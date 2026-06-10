#include "imageview.h"

#include "filelistmodel.h"
#include "imageio.h"

#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kZoomStep      = 1.25;
constexpr double kMaxScale      = 32.0;  // 3200% of actual pixels
constexpr int    kPanDivisor    = 8;     // keyboard pan step = viewport / 8
constexpr int    kBadgeMs       = 1000;
constexpr int    kBadgeMargin   = 12;
constexpr int    kCacheSettleMs = 150;   // smooth-downscale regen after zooming settles
}  // namespace

ImageView::ImageView(FileListModel *model, QWidget *parent)
    : QWidget(parent), m_model(model) {
    // The label only hosts QMovie playback (GIFs) and error text. Stills are
    // painted directly in paintEvent so zoom/pan can transform freely.
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet("background-color: black;");
    m_label->setMinimumSize(1, 1);
    m_label->hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_label);

    m_zoomBadge = new QLabel(this);
    m_zoomBadge->setStyleSheet(
        "background-color: rgba(0, 0, 0, 170); color: white;"
        "padding: 3px 8px; border-radius: 4px;");
    m_zoomBadge->hide();

    m_badgeTimer = new QTimer(this);
    m_badgeTimer->setSingleShot(true);
    m_badgeTimer->setInterval(kBadgeMs);
    connect(m_badgeTimer, &QTimer::timeout, m_zoomBadge, &QLabel::hide);

    m_cacheTimer = new QTimer(this);
    m_cacheTimer->setSingleShot(true);
    m_cacheTimer->setInterval(kCacheSettleMs);
    connect(m_cacheTimer, &QTimer::timeout, this, &ImageView::regenerateDownscaleCache);

    setFocusPolicy(Qt::StrongFocus);

    connect(m_model, &FileListModel::filesChanged, this, &ImageView::onFilesChanged);
    onFilesChanged();
}

ImageView::~ImageView() = default;

void ImageView::onFilesChanged() {
    // Identity-first: if the previously-shown image is still in the list,
    // follow it to its (possibly shifted) new index. Only fall back to
    // clamping the old index when the path is gone (e.g. it was deleted or
    // moved). Without this, reload() loads the wrong image once via the
    // signal and again via the explicit setIndex() that callers use to
    // restore position.
    if (!m_currentPath.isEmpty()) {
        const int newIdx = m_model->indexOf(m_currentPath);
        if (newIdx >= 0) {
            m_index = newIdx;
            loadCurrent();
            return;
        }
    }
    const int n = m_model->count();
    if (m_index >= n) m_index = n == 0 ? 0 : n - 1;
    if (m_index < 0)  m_index = 0;
    loadCurrent();
}

void ImageView::setIndex(int index) {
    if (m_model->isEmpty()) return;
    if (index < 0) index = 0;
    if (index >= m_model->count()) index = m_model->count() - 1;
    m_index = index;
    loadCurrent();
}

void ImageView::next() {
    if (m_model->isEmpty()) return;
    m_index = (m_index + 1) % m_model->count();
    loadCurrent();
}

void ImageView::previous() {
    if (m_model->isEmpty()) return;
    m_index = (m_index - 1 + m_model->count()) % m_model->count();
    loadCurrent();
}

void ImageView::loadCurrent() {
    m_currentPath = m_model->at(m_index);

    // Tear down the previous animation (if any) before loading the next path
    // so a navigation away from a GIF stops its decode/play loop promptly.
    m_movie.reset();
    m_label->setMovie(nullptr);
    m_original = QPixmap();
    m_movieNativeSize = QSize();
    m_downscaleCache = QPixmap();
    m_cacheTimer->stop();
    m_fitMode = true;
    m_dragging = false;
    unsetCursor();  // a key-driven load can interrupt a drag mid-press
    m_zoomBadge->hide();

    if (m_currentPath.isEmpty()) {
        m_label->clear();
        m_label->hide();
        update();
        return;
    }

    if (isGifPath(m_currentPath)) {
        m_movie = std::make_unique<QMovie>(m_currentPath);
        m_movie->setCacheMode(QMovie::CacheAll);
        if (m_movie->isValid()) {
            m_movieNativeSize = peekImageSize(m_currentPath);
            m_label->setStyleSheet("background-color: black;");
            m_label->setMovie(m_movie.get());
            m_label->show();
            applyMovieScale();
            m_movie->start();
        } else {
            m_movie.reset();
            showLoadError("Failed to load GIF");
        }
    } else {
        const QImage img = readImage(m_currentPath);
        m_original = img.isNull() ? QPixmap() : QPixmap::fromImage(img);
        if (m_original.isNull()) {
            showLoadError("Failed to load image");
        } else {
            m_label->hide();
            applyFit();
        }
    }
    update();
    emit currentChanged(m_index, m_currentPath);
}

void ImageView::showLoadError(const QString &text) {
    m_label->setText(text);
    m_label->setStyleSheet("background-color: black; color: #888;");
    m_label->show();
}

void ImageView::applyMovieScale() {
    // Native size is peeked once at load (QMovie::frameRect() isn't populated
    // until the first frame is jumped to). Scale to the viewport rather than
    // m_label: a freshly shown label hasn't been laid out yet, so its size()
    // is stale until the deferred LayoutRequest runs.
    if (!m_movieNativeSize.isValid()) return;
    QSize target = m_movieNativeSize;
    target.scale(size(), Qt::KeepAspectRatio);
    m_movie->setScaledSize(target);
}

double ImageView::fitScale() const {
    if (m_original.isNull() || width() <= 0 || height() <= 0) return 1.0;
    return std::min(double(width())  / m_original.width(),
                    double(height()) / m_original.height());
}

// The floor never exceeds 100% so zoomToActual() can always reach actual
// pixels; the ceiling never drops below fit so fit mode stays in range for
// images smaller than the viewport. Both together also keep std::clamp's
// lo <= hi precondition intact for any image/viewport combination.
double ImageView::minScale() const { return std::min(fitScale() / 4.0, 1.0); }
double ImageView::maxScale() const { return std::max(kMaxScale, fitScale()); }

void ImageView::applyFit() {
    m_fitMode = true;
    m_scale = fitScale();
    clampOffset();  // image never exceeds the viewport at fit → pure centering
    update();
}

void ImageView::applyScale(double scale, QPointF anchor) {
    scale = std::clamp(scale, minScale(), maxScale());
    // Keep the image point under the anchor fixed across the scale change.
    const QPointF imagePoint = (anchor - m_offset) / m_scale;
    m_offset = anchor - imagePoint * scale;
    m_scale = scale;
    m_fitMode = false;
    clampOffset();
    update();
}

void ImageView::zoomTo(double scale, QPointF anchor) {
    if (!hasStill()) return;
    applyScale(scale, anchor);
    showZoomBadge();
}

void ImageView::zoomIn()  { zoomTo(m_scale * kZoomStep, rect().center()); }
void ImageView::zoomOut() { zoomTo(m_scale / kZoomStep, rect().center()); }

void ImageView::zoomToFit() {
    if (!hasStill()) return;
    applyFit();
    showZoomBadge();
}

void ImageView::zoomToActual() { zoomTo(1.0, rect().center()); }

void ImageView::panBy(int sx, int sy) {
    if (!hasStill()) return;
    m_offset -= QPointF(sx * width() / kPanDivisor, sy * height() / kPanDivisor);
    clampOffset();
    update();
}

void ImageView::clampOffset() {
    if (!hasStill()) return;
    const double sw = m_original.width()  * m_scale;
    const double sh = m_original.height() * m_scale;
    // Per axis: center when the image fits, otherwise forbid gaps at the
    // edges. This also makes panning a natural no-op at fit zoom.
    if (sw <= width())
        m_offset.setX((width() - sw) / 2.0);
    else
        m_offset.setX(std::clamp(m_offset.x(), width() - sw, 0.0));
    if (sh <= height())
        m_offset.setY((height() - sh) / 2.0);
    else
        m_offset.setY(std::clamp(m_offset.y(), height() - sh, 0.0));
}

void ImageView::toggleInterpolation() {
    if (!hasStill()) return;
    m_nearest = !m_nearest;
    update();
    showBadge(m_nearest ? "Nearest" : "Bilinear");
}

void ImageView::showZoomBadge() {
    const int percent = qRound(m_scale * 100.0);
    showBadge(m_fitMode ? QString("Fit · %1%").arg(percent)
                        : QString("%1%").arg(percent));
}

void ImageView::showBadge(const QString &text) {
    m_zoomBadge->setText(text);
    m_zoomBadge->adjustSize();
    positionBadge();
    m_zoomBadge->show();
    m_zoomBadge->raise();
    m_badgeTimer->start();
}

void ImageView::positionBadge() {
    m_zoomBadge->move(width() - m_zoomBadge->width() - kBadgeMargin, kBadgeMargin);
}

QSize ImageView::downscaleTarget() const {
    return QSize(qMax(1, qRound(m_original.width()  * m_scale)),
                 qMax(1, qRound(m_original.height() * m_scale)));
}

void ImageView::regenerateDownscaleCache() {
    if (!hasStill() || m_scale >= 1.0) return;
    m_downscaleCache = m_original.scaled(
        downscaleTarget(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    update();
}

void ImageView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!hasStill()) return;

    if (m_scale < 1.0) {
        // Below 100% a smooth-scaled copy gives the best quality. The first
        // paint of an image builds it synchronously (the old fit-rescale
        // cost), but regenerating per wheel notch would thrash on large
        // images — mid-zoom paints fall through to a cheap viewport-bounded
        // transform and the cache regenerates once zooming settles.
        if (m_downscaleCache.isNull())
            m_downscaleCache = m_original.scaled(
                downscaleTarget(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (m_downscaleCache.size() == downscaleTarget()) {
            p.drawPixmap(QPoint(qRound(m_offset.x()), qRound(m_offset.y())),
                         m_downscaleCache);
            return;
        }
        m_cacheTimer->start();
        p.setRenderHint(QPainter::SmoothPixmapTransform);
    } else {
        // At or above 100%: bilinear by default; nearest-neighbor when the
        // user toggles it to inspect individual pixels.
        p.setRenderHint(QPainter::SmoothPixmapTransform, !m_nearest);
    }
    p.translate(m_offset);
    p.scale(m_scale, m_scale);
    p.drawPixmap(0, 0, m_original);
}

void ImageView::wheelEvent(QWheelEvent *event) {
    if (!hasStill()) { event->ignore(); return; }
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) { event->ignore(); return; }
    zoomTo(m_scale * std::pow(kZoomStep, steps), event->position());
    event->accept();
}

void ImageView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && hasStill()) {
        m_dragging = true;
        m_lastDragPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging) {
        m_offset += QPointF(event->pos() - m_lastDragPos);
        m_lastDragPos = event->pos();
        clampOffset();
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && hasStill()) {
        if (m_fitMode) zoomTo(1.0, event->position());
        else           zoomToFit();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ImageView::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_movie) {
        applyMovieScale();
    } else if (hasStill()) {
        if (m_fitMode) {
            applyFit();
        } else {
            // A resize moves both viewport edges symmetrically around the
            // center, so keeping the centered image point centered is a pure
            // translation; applyScale() then re-clamps against the resized
            // floor/ceiling without disturbing the anchor.
            const QSize old = event->oldSize();
            if (old.isValid())
                m_offset += QPointF(width() - old.width(),
                                    height() - old.height()) / 2.0;
            applyScale(m_scale, QPointF(width() / 2.0, height() / 2.0));
        }
    }
    positionBadge();
}
