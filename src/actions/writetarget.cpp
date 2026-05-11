#include "writetarget.h"

#include "actionlogger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace WriteTarget {

Resolved resolve(const QString &outDir,
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
    // when output dir == source dir); write() goes through a sibling .part
    // file so the source is intact until the rename succeeds.
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

QString write(const QString &finalPath,
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

QString move(const QString &input,
             const QString &finalPath,
             ActionLogger *logger,
             const char *verb) {
    QDir().mkpath(QFileInfo(finalPath).absolutePath());
    // QFile::rename refuses to overwrite, so clear any pre-existing target
    // (the caller's overwrite policy has already decided we're allowed to).
    if (QFile::exists(finalPath)) QFile::remove(finalPath);
    if (QFile::rename(input, finalPath)) {
        if (logger) logger->info(QString("%1 %2 -> %3")
                                     .arg(QString::fromLatin1(verb),
                                          input, finalPath));
        return finalPath;
    }
    // Same-fs rename failed (typically cross-fs): copy via the .part protocol
    // so the source is intact until the destination is finalised, then unlink
    // the source.
    const QString out = write(finalPath, logger,
        [&input](const QString &temp) { return QFile::copy(input, temp); });
    if (out.isEmpty()) return {};
    QFile::remove(input);
    return out;
}

}  // namespace WriteTarget
