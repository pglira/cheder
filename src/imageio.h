#pragma once

#include <QImage>
#include <QImageReader>
#include <QString>

// Decode `path` into a QImage, honoring EXIF orientation. Returns a null
// QImage on failure. Single point so the QImageReader + setAutoTransform(true)
// pattern doesn't live in six different .cpp files.
inline QImage readImage(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}
