#pragma once

#include <QImage>
#include <QImageReader>
#include <QSize>
#include <QString>

// Decode `path` into a QImage, honoring EXIF orientation. Returns a null
// QImage on failure. Single point so the QImageReader + setAutoTransform(true)
// pattern doesn't live in six different .cpp files.
inline QImage readImage(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

// Read only the on-disk dimensions of `path` without decoding pixels. Returns
// an invalid QSize on failure. Used by same-size validation in Crop and
// Animation so the whole selection can be inspected before any expensive
// decode work begins.
inline QSize peekImageSize(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.size();
}
