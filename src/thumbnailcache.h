#pragma once

#include <QImage>
#include <QString>

// Disk-backed thumbnail cache.
//
// Thumbnails are cached at a small set of fixed sizes rather than at whatever
// size the caller asked for, so zooming the grid reuses one cache entry across
// a range of icon sizes instead of invalidating it. Each bucket lives in its
// own directory: ~/.cache/cheder/thumbs/<bucket>/<sha1-of-path>.png. The source
// mtime and a schema version are written into the PNG's text chunks, so edits
// to the source and changes to the compositing logic both invalidate
// transparently.
//
// thumbnail() returns an image fitted within the requested size, honoring EXIF
// orientation and never upscaling; callers needing a square slot must pad
// themselves. It returns a QImage rather than a QPixmap so it can be called
// off the GUI thread, which is where the decoding cost belongs.
//
// Reentrant: state is fixed after construction, so several threads may call
// thumbnail() concurrently. Concurrent callers requesting the same uncached
// image both compute it and both write the cache file; writes are atomic, so
// the duplicated work is wasted but harmless.
class ThumbnailCache {
public:
    ThumbnailCache();

    QImage thumbnail(const QString &sourcePath, int requestedSize);

private:
    QString cachePathFor(const QString &sourcePath, int bucket) const;

    QString m_cacheDir;
};
