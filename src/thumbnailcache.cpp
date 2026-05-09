#include "thumbnailcache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QStandardPaths>
#include <QUrl>

namespace {
constexpr int kCanonicalMaxSize = 512;
constexpr int kCacheSchemaVersion = 2;   // bump to invalidate old cropped cache files

const char *const kMTimeKey   = "Thumb::MTime";
const char *const kUriKey     = "Thumb::URI";
const char *const kVersionKey = "Thumb::Cheder::Version";

// Decode the source and shrink it to fit kCanonicalMaxSize, preserving
// aspect ratio and honoring EXIF orientation. Already-small sources are
// returned untouched — never upscale a thumbnail.
QImage composeCanonical(const QString &sourcePath) {
    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) return QImage();

    if (img.width() <= kCanonicalMaxSize && img.height() <= kCanonicalMaxSize)
        return img;
    return img.scaled(QSize(kCanonicalMaxSize, kCanonicalMaxSize),
                      Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
}  // namespace

ThumbnailCache::ThumbnailCache() {
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbs";
    QDir().mkpath(m_cacheDir);
}

QString ThumbnailCache::cachePathFor(const QString &sourcePath) const {
    const QByteArray hash = QCryptographicHash::hash(sourcePath.toUtf8(),
                                                     QCryptographicHash::Sha1).toHex();
    return m_cacheDir + '/' + QString::fromLatin1(hash) + ".png";
}

QPixmap ThumbnailCache::getThumbnail(const QString &sourcePath, QSize maxSize) {
    const QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists()) return QPixmap();
    const qint64 srcMtime = srcInfo.lastModified().toSecsSinceEpoch();

    const QString cachePath = cachePathFor(sourcePath);
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
        canonical = composeCanonical(sourcePath);
        if (canonical.isNull()) return QPixmap();
        canonical.setText(kMTimeKey,   QString::number(srcMtime));
        canonical.setText(kUriKey,     QUrl::fromLocalFile(srcInfo.absoluteFilePath()).toString());
        canonical.setText(kVersionKey, QString::number(kCacheSchemaVersion));
        canonical.save(cachePath, "PNG");
    }

    // Aspect-correct fit within maxSize. Returned pixmap is at most maxSize
    // in each dimension; callers that need a square slot must pad themselves.
    QImage scaled = canonical.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap pm = QPixmap::fromImage(std::move(scaled));
    pm.setDevicePixelRatio(1.0);
    return pm;
}
