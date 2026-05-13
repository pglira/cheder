#pragma once

#include "writetarget.h"

#include <QKeySequence>
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

    // Optional global shortcut. Empty = no shortcut. Must be a SINGLE
    // keystroke (Alt+R, Ctrl+K, etc.) — only seq[0] is consulted by both the
    // KeyDispatcher binding and the ActionBar label. Multi-keystroke chord
    // sequences are not supported. No collision check across actions; if
    // two actions declare the same shortcut, the first-registered wins.
    virtual QKeySequence shortcut() const { return {}; }

    virtual bool acceptsCount(int n) const = 0;

    // When true, the dialog shows an Apply / Close button pair instead of
    // OK / Cancel: Apply runs the action and keeps the dialog open so the
    // user can iterate on parameters without re-entering the action.
    // Multi-apply configure() implementations call apply() themselves from
    // their Apply handler; MainWindow then skips its own post-configure
    // apply() call. Default false preserves the original single-shot flow.
    virtual bool supportsMultiApply() const { return false; }

    // Show the parameter dialog. Returns false on cancel. `inputs` lets the
    // dialog summarise what it will operate on; `defaultOutDir` is the
    // suggested output (first input's parent dir + this action's id()).
    // `logger` is forwarded so multi-apply implementations can invoke
    // apply() from inside the dialog's Apply button; single-shot actions
    // ignore it and let MainWindow drive apply() after configure() returns.
    virtual bool configure(QWidget *parent,
                           const QStringList &inputs,
                           const QString &defaultOutDir,
                           ActionLogger *logger) = 0;

    // Run on `inputs`; return the paths actually written. `logger` may be
    // null; subclasses should use it to report per-file outcomes.
    virtual QStringList apply(const QStringList &inputs, ActionLogger *logger) = 0;
};

// One input -> one output. Subclasses just implement applyOne(); the base
// class owns the loop, the overwrite policy, the .part temp-file protocol,
// and the per-file logging. Policy + atomic-write live in WriteTarget; this
// class adds 1→1 conveniences on top (in-place same-file guard, per-run
// skipped/failed counters folded into the summary).
class BatchAction : public Action {
public:
    bool acceptsCount(int n) const override { return n >= 1; }

    QStringList apply(const QStringList &inputs, ActionLogger *logger) override;

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

    QString              m_outDir;
    WriteTarget::Overwrite m_overwrite = WriteTarget::Overwrite::Overwrite;
    // Optional output-filename template with `{stem}` / `{ext}` placeholders;
    // rendered per input inside resolveOutputPath. Empty (the default) keeps
    // the input's filename verbatim — used by Copy/Move which have no
    // filename UI.
    QString              m_outFilenameTemplate;

private:
    // Reset at the start of each apply(); inspected at the end for the
    // summary entry. Updated by resolveOutputPath/writeOne so that custom
    // applyOne() overrides that do their own writing still flow through
    // these helpers and stay accounted-for.
    int m_skippedThisRun = 0;
    int m_failedThisRun  = 0;
};
