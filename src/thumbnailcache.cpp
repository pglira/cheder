#include "thumbnailcache.h"

#include "exifthumbnail.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <iterator>

namespace {
// Long-edge sizes thumbnails are cached at. The largest matches the grid's
// maximum icon size, so every request maps onto a bucket.
constexpr int kBuckets[] = {128, 256, 512};
constexpr int kCacheSchemaVersion = 3;   // bump to invalidate stale cache files

const char *const kMTimeKey   = "Thumb::MTime";
const char *const kUriKey     = "Thumb::URI";
const char *const kVersionKey = "Thumb::Cheder::Version";

// Smallest bucket that can serve `size`.
int bucketFor(int size) {
    for (const int bucket : kBuckets)
        if (size <= bucket) return bucket;
    return kBuckets[std::size(kBuckets) - 1];
}

// Shrink to fit a `bucket`-sided box, preserving aspect ratio. Already-small
// images are returned untouched — never upscale a thumbnail.
QImage fitWithin(const QImage &image, int bucket) {
    if (image.width() <= bucket && image.height() <= bucket) return image;
    return image.scaled(QSize(bucket, bucket), Qt::KeepAspectRatio,
                        Qt::SmoothTransformation);
}

// Produce a thumbnail for `sourcePath` no larger than `bucket` on its long
// edge, honoring EXIF orientation.
QImage composeThumbnail(const QString &sourcePath, int bucket) {
    // A camera's embedded thumbnail costs a few hundred microseconds against
    // tens or hundreds of milliseconds for a full decode, so it wins outright
    // whenever it is large enough to fill the bucket.
    const QImage embedded = readEmbeddedThumbnail(sourcePath);
    if (!embedded.isNull()
        && std::max(embedded.width(), embedded.height()) >= bucket)
        return fitWithin(embedded, bucket);

    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);

    // size() reports the stored dimensions, before any orientation transform,
    // which is also the space setScaledSize() is interpreted in — so the two
    // stay consistent and the rotation afterwards preserves the long edge.
    const QSize stored = reader.size();
    if (stored.isValid()) {
        const bool canScaleWhileDecoding =
            reader.supportsOption(QImageIOHandler::ScaledSize)
            && std::max(stored.width(), stored.height()) > bucket;
        if (canScaleWhileDecoding) {
            // Decoding straight to the target size lets the JPEG handler pick
            // a DCT scaling factor instead of reconstructing every pixel. It
            // also keeps the working buffer small enough that oversized
            // sources stay clear of Qt's allocation limit.
            QSizeF scaled(stored);
            scaled.scale(bucket, bucket, Qt::KeepAspectRatio);
            if (!scaled.isEmpty()) reader.setScaledSize(scaled.toSize());
        } else {
            // Formats that decode at full size only: reject sources whose raw
            // buffer would exceed Qt's allocation limit rather than let the
            // reader attempt and refuse the allocation, a path that corrupts
            // memory in a way that is only sometimes fatal.
            const qint64 maxBytes =
                qint64(QImageReader::allocationLimit()) * 1024 * 1024;
            const qint64 rawBytes =
                qint64(stored.width()) * qint64(stored.height()) * 4;
            if (maxBytes > 0 && rawBytes > maxBytes) return QImage();
        }
    }

    const QImage decoded = reader.read();
    if (decoded.isNull()) return QImage();
    return fitWithin(decoded, bucket);
}
}  // namespace

ThumbnailCache::ThumbnailCache() {
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbs";
    for (const int bucket : kBuckets)
        QDir().mkpath(m_cacheDir + '/' + QString::number(bucket));
}

QString ThumbnailCache::cachePathFor(const QString &sourcePath, int bucket) const {
    const QByteArray hash = QCryptographicHash::hash(sourcePath.toUtf8(),
                                                     QCryptographicHash::Sha1).toHex();
    return m_cacheDir + '/' + QString::number(bucket) + '/'
           + QString::fromLatin1(hash) + ".png";
}

QImage ThumbnailCache::thumbnail(const QString &sourcePath, int requestedSize) {
    const QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists()) return QImage();
    const qint64 srcMtime = srcInfo.lastModified().toSecsSinceEpoch();

    const int bucket = bucketFor(requestedSize);
    const QString cachePath = cachePathFor(sourcePath, bucket);
    QImage canonical;

    if (QFileInfo::exists(cachePath)) {
        QImageReader reader(cachePath);
        QImage cached = reader.read();
        if (!cached.isNull()
            && cached.text(kMTimeKey).toLongLong() == srcMtime
            && cached.text(kVersionKey).toInt() == kCacheSchemaVersion) {
            canonical = std::move(cached);
        }
    }

    if (canonical.isNull()) {
        canonical = composeThumbnail(sourcePath, bucket);
        if (canonical.isNull()) return QImage();
        canonical.setText(kMTimeKey,   QString::number(srcMtime));
        canonical.setText(kUriKey,     QUrl::fromLocalFile(srcInfo.absoluteFilePath()).toString());
        canonical.setText(kVersionKey, QString::number(kCacheSchemaVersion));
        // Written atomically so a concurrent reader never sees a half-written
        // file and a crash mid-write leaves no corrupt entry behind.
        QSaveFile out(cachePath);
        if (out.open(QIODevice::WriteOnly) && canonical.save(&out, "PNG"))
            out.commit();
    }

    return fitWithin(canonical, requestedSize);
}
