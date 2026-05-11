#include "action.h"

#include "actionlogger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

QStringList BatchAction::apply(const QStringList &inputs, ActionLogger *logger) {
    if (logger) logger->beginRun(name(), inputs.size());

    m_skippedThisRun = 0;
    m_failedThisRun  = 0;

    QStringList outputs;
    outputs.reserve(inputs.size());
    for (const QString &in : inputs) {
        const QString out = applyOne(in, logger);
        if (!out.isEmpty()) outputs << out;
    }

    // Anything we lost that wasn't accounted for as skip is a failure.
    const int written = outputs.size();
    int failed  = m_failedThisRun;
    int skipped = m_skippedThisRun;
    const int unaccounted = inputs.size() - written - failed - skipped;
    if (unaccounted > 0) failed += unaccounted;

    if (logger) logger->endRun(name(), written, skipped, failed);
    return outputs;
}

BatchAction::ResolvedDest BatchAction::resolveDestPath(const QString &outDir,
                                                       const QString &filename,
                                                       Overwrite policy,
                                                       ActionLogger *logger,
                                                       const QString &avoidIfSame) {
    const QString candidate = outDir + '/' + filename;

    auto sameFile = [](const QString &a, const QString &b) {
        return QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
    };

    if (!QFile::exists(candidate)) return {candidate, ResolveStatus::Ok};

    // Candidate exists. With Overwrite that's fine (including in-place edits
    // when output dir == source dir); writeAtomically() goes through a sibling
    // .part file so the source is intact until the rename succeeds.
    switch (policy) {
    case Overwrite::Overwrite:
        return {candidate, ResolveStatus::Ok};
    case Overwrite::Skip:
        if (logger) logger->info(QString("skip %1 — already exists").arg(filename));
        return {{}, ResolveStatus::Skip};
    case Overwrite::Rename: {
        const QFileInfo fi(candidate);
        const QString stem   = fi.completeBaseName();
        const QString suffix = fi.suffix();
        const QString sep    = suffix.isEmpty() ? QString() : QStringLiteral(".");
        for (int n = 1; n < 10000; ++n) {
            const QString alt = QString("%1/%2_%3%4%5")
                                    .arg(outDir, stem)
                                    .arg(n)
                                    .arg(sep, suffix);
            if (!avoidIfSame.isEmpty() && sameFile(alt, avoidIfSame)) continue;
            if (!QFile::exists(alt))                                  return {alt, ResolveStatus::Ok};
        }
        if (logger) logger->error(QString("skip %1 — exhausted rename suffixes").arg(filename));
        return {{}, ResolveStatus::Failed};
    }
    }
    return {{}, ResolveStatus::Failed};
}

QString BatchAction::writeAtomically(const QString &finalPath,
                                     ActionLogger *logger,
                                     std::function<bool(const QString &)> writer) {
    // Insert ".part" *before* the extension so format-from-extension writers
    // (QImageWriter, etc.) still detect the correct format. "foo.jpg" ->
    // "foo.part.jpg"; extensionless "foo" -> "foo.part".
    const QFileInfo finalInfo(finalPath);
    const QString suffix = finalInfo.suffix();
    const QString tempPath = suffix.isEmpty()
        ? finalPath + ".part"
        : finalInfo.path() + '/' + finalInfo.completeBaseName() + ".part." + suffix;
    if (QFile::exists(tempPath)) QFile::remove(tempPath);

    if (!writer(tempPath)) {
        QFile::remove(tempPath);
        if (logger) logger->error(QString("failed %1 — write error")
                                      .arg(finalInfo.fileName()));
        return {};
    }

    if (QFile::exists(finalPath)) QFile::remove(finalPath);
    if (!QFile::rename(tempPath, finalPath)) {
        QFile::remove(tempPath);
        if (logger) logger->error(QString("failed %1 — could not finalize %2")
                                      .arg(finalInfo.fileName(), finalPath));
        return {};
    }

    if (logger) logger->info(QString("wrote %1").arg(finalPath));
    return finalPath;
}

QString BatchAction::resolveOutputPath(const QString &input, ActionLogger *logger) {
    const auto r = resolveDestPath(m_outDir, QFileInfo(input).fileName(),
                                   m_overwrite, logger, input);
    if (r.status == ResolveStatus::Skip)   ++m_skippedThisRun;
    if (r.status == ResolveStatus::Failed) ++m_failedThisRun;
    return r.path;
}

QString BatchAction::writeOne(const QString &input,
                              ActionLogger *logger,
                              std::function<bool(const QString &)> writer) {
    const QString finalPath = resolveOutputPath(input, logger);
    if (finalPath.isEmpty()) return {};

    QDir().mkpath(m_outDir);
    const QString out = writeAtomically(finalPath, logger, writer);
    if (out.isEmpty()) ++m_failedThisRun;
    return out;
}
