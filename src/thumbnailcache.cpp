#include "thumbnailcache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QStandardPaths>
#include <QUrl>

namespace {
constexpr int kCanonicalSize = 512;
const char *const kMTimeKey = "Thumb::MTime";
const char *const kUriKey   = "Thumb::URI";

QImage composeCanonical(const QString &sourcePath) {
    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) return QImage();

    const QSize target(kCanonicalSize, kCanonicalSize);
    const QImage scaled = img.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = (scaled.width()  - target.width())  / 2;
    const int y = (scaled.height() - target.height()) / 2;
    return scaled.copy(x, y, target.width(), target.height());
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

QPixmap ThumbnailCache::getThumbnail(const QString &sourcePath, QSize displaySize) {
    const QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists()) return QPixmap();
    const qint64 srcMtime = srcInfo.lastModified().toSecsSinceEpoch();

    const QString cachePath = cachePathFor(sourcePath);
    QImage canonical;

    if (QFileInfo::exists(cachePath)) {
        QImageReader reader(cachePath);
        QImage cached = reader.read();
        if (!cached.isNull() && cached.text(kMTimeKey).toLongLong() == srcMtime)
            canonical = std::move(cached);
    }

    if (canonical.isNull()) {
        canonical = composeCanonical(sourcePath);
        if (canonical.isNull()) return QPixmap();
        canonical.setText(kMTimeKey, QString::number(srcMtime));
        canonical.setText(kUriKey,   QUrl::fromLocalFile(srcInfo.absoluteFilePath()).toString());
        canonical.save(cachePath, "PNG");
    }

    QImage scaled = canonical.scaled(displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap pm = QPixmap::fromImage(std::move(scaled));
    pm.setDevicePixelRatio(1.0);
    return pm;
}
