#pragma once

#include <QPixmap>
#include <QSize>
#include <QString>

// Disk-backed cache for square thumbnails.
//
// Stores one canonical 512x512 center-cropped PNG per source image at
// ~/.cache/cheder/thumbs/<sha1-of-path>.png, with the source mtime saved in
// the PNG's "Thumb::MTime" text chunk. getThumbnail() returns a pixmap
// downscaled to the requested display size; the cache is invalidated
// transparently when the source's mtime changes.
class ThumbnailCache {
public:
    ThumbnailCache();

    QPixmap getThumbnail(const QString &sourcePath, QSize displaySize);

private:
    QString cachePathFor(const QString &sourcePath) const;

    QString m_cacheDir;
};
