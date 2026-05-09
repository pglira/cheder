#pragma once

#include <QByteArray>
#include <QDir>
#include <QImageReader>
#include <QString>
#include <QStringList>

// Glob patterns ("*.png", "*.jpg", ...) for every image format Qt's installed
// plugins can decode. Used both by the startup directory scan and by the
// in-app reload.
inline QStringList supportedImageGlobs() {
    QStringList globs;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        globs << "*." + QString::fromLatin1(fmt).toLower();
    return globs;
}

// Returns absolute paths of every image file directly inside `dir` (does not
// recurse). Sorted by filename, case-insensitive.
inline QStringList listImagesInDir(const QString &dir) {
    QDir d(dir);
    const QStringList names = d.entryList(supportedImageGlobs(),
                                          QDir::Files | QDir::Readable,
                                          QDir::Name | QDir::IgnoreCase);
    QStringList absolute;
    absolute.reserve(names.size());
    for (const QString &n : names) absolute << d.absoluteFilePath(n);
    return absolute;
}
