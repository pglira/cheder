#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class ActionLogger;
class QWidget;

// Image-processing operation invoked from the action bar. Subclasses own
// their parameter dialog and execution. Per-input actions should subclass
// BatchAction to avoid reimplementing the loop, the overwrite policy, and
// the temp-file write protocol.
class Action {
public:
    virtual ~Action() = default;

    // Stable identifier (used by the action bar's filter).
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString description() const { return {}; }

    virtual bool acceptsCount(int n) const = 0;

    // Show the parameter dialog. Returns false on cancel. `inputs` lets the
    // dialog summarise what it will operate on; `defaultOutDir` is the
    // suggested output (first input's parent dir + this action's id()).
    virtual bool configure(QWidget *parent,
                           const QStringList &inputs,
                           const QString &defaultOutDir) = 0;

    // Run on `inputs`; return the paths actually written. `logger` may be
    // null; subclasses should use it to report per-file outcomes.
    virtual QStringList apply(const QStringList &inputs, ActionLogger *logger) = 0;
};

// One input -> one output. Subclasses just implement applyOne(); the base
// class owns the loop, the overwrite policy, the .part temp-file protocol,
// and the per-file logging.
class BatchAction : public Action {
public:
    enum class Overwrite { Overwrite, Skip, Rename };

    // Outcome of resolving where to write. The fan-in case (Concatenate) reads
    // `status` to decide whether the summary line should report a skip vs a
    // failure; BatchAction uses it to bump its per-run counters.
    enum class ResolveStatus { Ok, Skip, Failed };
    struct ResolvedDest {
        QString       path;    // empty unless status == Ok
        ResolveStatus status;
    };

    bool acceptsCount(int n) const override { return n >= 1; }

    QStringList apply(const QStringList &inputs, ActionLogger *logger) override;

    // Resolve `outDir/filename` under `policy`. Logs the reason on skip/fail.
    // `avoidIfSame` (when non-empty) makes Rename mode skip candidates that
    // resolve to that path — used by 1→1 in-place edits so the rename loop
    // can't pick the source file itself.
    static ResolvedDest resolveDestPath(const QString &outDir,
                                        const QString &filename,
                                        Overwrite policy,
                                        ActionLogger *logger,
                                        const QString &avoidIfSame = {});

    // Write to a sibling ".part" file, then rename into place. Returns
    // `finalPath` on success and logs "wrote …"; on failure returns {} and
    // logs the reason. Callers are responsible for ensuring the parent
    // directory exists (e.g., QDir().mkpath(outDir)).
    static QString writeAtomically(const QString &finalPath,
                                   ActionLogger *logger,
                                   std::function<bool(const QString &tempPath)> writer);

protected:
    // Return the produced output path, or {} on failure/skip. Implementations
    // should route their actual write through writeOne() so the temp-file
    // protocol and overwrite policy apply uniformly.
    virtual QString applyOne(const QString &input, ActionLogger *logger) = 0;

    // Resolves the destination path under m_outDir, applying m_overwrite.
    // Returns {} (and logs a skip/warn entry, bumping m_skippedThisRun) if
    // the file exists and policy is Skip, or if the resolved path would
    // equal `input` itself.
    QString resolveOutputPath(const QString &input, ActionLogger *logger);

    // Resolves the destination, creates m_outDir, invokes `writer(tempPath)`
    // on a sibling ".part" file, then renames into place. Returns the final
    // path on success or {} on skip/failure (with the reason logged and the
    // appropriate per-run counter bumped).
    QString writeOne(const QString &input,
                     ActionLogger *logger,
                     std::function<bool(const QString &tempPath)> writer);

    QString   m_outDir;
    Overwrite m_overwrite = Overwrite::Overwrite;

private:
    // Reset at the start of each apply(); inspected at the end for the
    // summary entry. Updated by resolveOutputPath/writeOne so that custom
    // applyOne() overrides that do their own writing still flow through
    // these helpers and stay accounted-for.
    int m_skippedThisRun = 0;
    int m_failedThisRun  = 0;
};
