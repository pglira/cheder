#pragma once

#include <QPixmap>
#include <QSize>
#include <QString>

// Disk-backed thumbnail cache.
//
// Stores one aspect-preserving canonical PNG per source image at
// ~/.cache/cheder/thumbs/<sha1-of-path>.png (long edge ≤ 512 px). The source
// mtime and a schema version are written into the PNG's text chunks, so
// edits to the source and changes to the cropping logic both invalidate
// transparently. getThumbnail() returns a pixmap fitted within the requested
// size; callers needing a square slot must pad themselves.
class ThumbnailCache {
public:
    ThumbnailCache();

    QPixmap getThumbnail(const QString &sourcePath, QSize maxSize);

private:
    QString cachePathFor(const QString &sourcePath) const;

    QString m_cacheDir;
};
