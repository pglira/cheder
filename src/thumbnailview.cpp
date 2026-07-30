#include "thumbnailview.h"

#include "filelistmodel.h"
#include "imageio.h"
#include "thumbnailcache.h"

#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QThread>
#include <QWheelEvent>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

constexpr int kDefaultThumbSize = 128;
constexpr int kThumbSizeStep    = 32;
constexpr int kThumbSizeMin     = 64;
constexpr int kThumbSizeMax     = 512;
constexpr int kGridPaddingW     = 8;
constexpr int kGridPaddingH     = 8;

// Scrolling and resizing emit their signals in bursts; this is how long the
// view waits for one to settle before deciding what to decode.
constexpr int kScheduleDelayMs = 50;

const QColor kPlaceholderColor(40, 40, 40);

QSize gridSizeFor(int thumbSize) {
    return QSize(thumbSize + kGridPaddingW, thumbSize + kGridPaddingH);
}

QPixmap placeholderPixmap(QSize size) {
    QPixmap pm(size);
    pm.setDevicePixelRatio(1.0);
    pm.fill(kPlaceholderColor);
    return pm;
}

// Paints a translucent play-triangle badge in the lower-right of `p`'s
// device so animated-GIF thumbnails are visually distinguishable from
// still images at a glance.
void drawAnimatedBadge(QPainter &p, QSize iconSize) {
    const int badgeSize = std::max(16, iconSize.width() / 6);
    const QRect badgeRect(iconSize.width()  - badgeSize - 4,
                          iconSize.height() - badgeSize - 4,
                          badgeSize, badgeSize);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 160));
    p.drawRoundedRect(badgeRect, 4, 4);
    p.setBrush(Qt::white);
    const int cx = badgeRect.center().x();
    const int cy = badgeRect.center().y();
    const int s  = badgeSize / 3;
    QPolygon tri;
    tri << QPoint(cx - s / 2, cy - s)
        << QPoint(cx - s / 2, cy + s)
        << QPoint(cx + s,     cy);
    p.drawPolygon(tri);
}

}  // namespace

ThumbnailView::ThumbnailView(FileListModel *model, QWidget *parent)
    : QListWidget(parent), m_model(model), m_cache(std::make_unique<ThumbnailCache>()) {
    setViewMode(QListView::IconMode);
    setIconSize(QSize(kDefaultThumbSize, kDefaultThumbSize));
    setGridSize(gridSizeFor(kDefaultThumbSize));
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSpacing(4);
    setWordWrap(true);
    setTextElideMode(Qt::ElideMiddle);

    // Leave a core to the GUI thread so decoding never starves painting and
    // input handling.
    m_pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));

    m_scheduleTimer.setSingleShot(true);
    m_scheduleTimer.setInterval(kScheduleDelayMs);
    connect(&m_scheduleTimer, &QTimer::timeout, this,
            &ThumbnailView::generateThumbnailsForItems);

    connect(m_model, &FileListModel::filesChanged, this, &ThumbnailView::onFilesChanged);
    onFilesChanged();
}

ThumbnailView::~ThumbnailView() {
    // Jobs hold `this` and the cache, so they must all be done before any
    // member is destroyed. Bumping the generation first means the ones already
    // running discard their results instead of touching a half-torn-down view.
    ++m_generation;
    m_pool.clear();
    m_pool.waitForDone();
}

int ThumbnailView::thumbnailSize() const {
    return iconSize().width();
}

void ThumbnailView::setThumbnailSize(int size) {
    size = std::clamp(size, kThumbSizeMin, kThumbSizeMax);
    if (size == thumbnailSize()) return;
    setIconSize(QSize(size, size));
    setGridSize(gridSizeFor(size));
    rebuildItems();
    scheduleThumbnailGeneration();
}

void ThumbnailView::zoomIn()  { setThumbnailSize(thumbnailSize() + kThumbSizeStep); }
void ThumbnailView::zoomOut() { setThumbnailSize(thumbnailSize() - kThumbSizeStep); }

void ThumbnailView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const int dy = event->angleDelta().y();
        if (dy > 0)      zoomIn();
        else if (dy < 0) zoomOut();
        event->accept();
        return;
    }
    QListWidget::wheelEvent(event);
}

void ThumbnailView::onFilesChanged() {
    rebuildItems();
    // A pass over a hidden grid decodes nothing: the viewport geometry it would
    // scan is not meaningful yet, so generateThumbnailsForItems() bails out and
    // showEvent() asks again once the grid is up. That keeps the startup
    // window-mapping sequence free of background decodes when cheder opens
    // straight into the image view.
    scheduleThumbnailGeneration();
}

void ThumbnailView::showEvent(QShowEvent *event) {
    QListWidget::showEvent(event);
    scheduleThumbnailGeneration();
}

void ThumbnailView::resizeEvent(QResizeEvent *event) {
    QListWidget::resizeEvent(event);
    // Resizing reflows the grid, so a different set of items is on screen.
    scheduleThumbnailGeneration();
}

void ThumbnailView::scrollContentsBy(int dx, int dy) {
    QListWidget::scrollContentsBy(dx, dy);
    scheduleThumbnailGeneration();
}

int ThumbnailView::firstSelectedRow() const {
    int min = -1;
    for (auto *it : selectedItems()) {
        const int r = row(it);
        if (min < 0 || r < min) min = r;
    }
    return min;
}

QString ThumbnailView::pathAt(int row) const {
    if (auto *it = item(row)) return it->data(Qt::UserRole).toString();
    return {};
}

QStringList ThumbnailView::selectedPaths() const {
    QList<QListWidgetItem *> items = selectedItems();
    std::sort(items.begin(), items.end(),
              [this](auto *a, auto *b) { return row(a) < row(b); });
    QStringList paths;
    paths.reserve(items.size());
    for (auto *it : items) paths << it->data(Qt::UserRole).toString();
    return paths;
}

void ThumbnailView::rebuildItems() {
    QSet<QString> previouslySelected;
    for (auto *it : selectedItems())
        previouslySelected.insert(it->data(Qt::UserRole).toString());
    // Track the current item by path, not by row: a rename can shift the
    // file's position in the sorted listing, so the old index would point
    // at a different file (selection is preserved by path for the same
    // reason).
    const int prevCurrentRow = currentRow();
    const QString prevCurrentPath =
        prevCurrentRow >= 0 ? item(prevCurrentRow)->data(Qt::UserRole).toString() : QString();

    // Every item is about to be recreated carrying a placeholder icon, so
    // results still in flight describe a grid that no longer exists.
    ++m_generation;
    m_loaded.clear();
    m_inFlight.clear();
    m_placeholder = placeholderPixmap(iconSize());

    // Build the new items off-widget first (old items — and thus old
    // rows/paths — stay readable via item()/count() below) and index them by
    // path for O(1) lookups instead of a linear indexOf() per candidate.
    const QStringList &files = m_model->files();
    QHash<QString, int> newRowOf;
    newRowOf.reserve(files.size());
    QList<QListWidgetItem *> newItems;
    newItems.reserve(files.size());
    for (int i = 0; i < files.size(); ++i) {
        const QString &path = files.at(i);
        auto *it = new QListWidgetItem(QIcon(m_placeholder), QString());
        it->setData(Qt::UserRole, path);
        it->setToolTip(path);
        it->setSizeHint(gridSize());
        if (previouslySelected.contains(path)) it->setSelected(true);
        newItems << it;
        newRowOf.insert(path, i);
    }

    int currentRowToRestore = newRowOf.value(prevCurrentPath, -1);
    if (currentRowToRestore < 0 && prevCurrentRow >= 0) {
        // The previously-current file is gone: scan outward through the old
        // widget ordering for the nearest survivor, preferring the next file
        // so a delete or move-away advances the cursor instead of stalling
        // it.
        const int oldCount = count();
        for (int i = prevCurrentRow + 1; i < oldCount && currentRowToRestore < 0; ++i)
            currentRowToRestore = newRowOf.value(item(i)->data(Qt::UserRole).toString(), -1);
        for (int i = prevCurrentRow - 1; i >= 0 && currentRowToRestore < 0; --i)
            currentRowToRestore = newRowOf.value(item(i)->data(Qt::UserRole).toString(), -1);
    }

    clear();
    for (auto *it : newItems) addItem(it);
    m_rowOf = std::move(newRowOf);
    if (currentRowToRestore >= 0)
        setCurrentRow(currentRowToRestore);
}

void ThumbnailView::scheduleThumbnailGeneration() {
    if (!m_scheduleTimer.isActive()) m_scheduleTimer.start();
}

void ThumbnailView::generateThumbnailsForItems() {
    if (!isVisible() || count() == 0) return;

    const QRect visible = viewport()->rect();
    const int visibleSurface = visible.width() * visible.height();
    const QPoint origin = visible.center();
    // Decode a screen's worth beyond the viewport so scrolling lands on
    // thumbnails that are already there, and release anything twice that far
    // out so a large directory does not accumulate every icon it ever showed.
    const int prefetchDistance = visible.bottomRight().manhattanLength();
    const int discardDistance  = prefetchDistance * 2;

    std::vector<std::pair<int, int>> candidates;   // (distance, row)

    for (int r = 0; r < count(); ++r) {
        auto *it = item(r);
        const QString path = it->data(Qt::UserRole).toString();
        const QRect rect = visualItemRect(it);

        int distance;
        if (rect.intersects(visible)) {
            // On screen: work left to right, top to bottom, so thumbnails
            // appear in the order the eye reads them.
            distance = rect.top() * visible.width() + rect.left();
        } else {
            const int away = (rect.center() - origin).manhattanLength();
            if (away > discardDistance) {
                if (m_loaded.remove(path)) it->setIcon(QIcon(m_placeholder));
                continue;
            }
            if (away > prefetchDistance) continue;
            // Offset past the largest possible on-screen distance so every
            // visible item is decoded before any prefetched one.
            distance = visibleSurface + away;
        }

        if (m_loaded.contains(path) || m_inFlight.contains(path)) continue;
        candidates.emplace_back(distance, r);
    }

    if (candidates.empty()) return;
    std::sort(candidates.begin(), candidates.end());

    const qint64 generation = m_generation;
    const int size = iconSize().width();
    ThumbnailCache *cache = m_cache.get();

    for (const auto &[distance, r] : candidates) {
        const QString path = item(r)->data(Qt::UserRole).toString();
        m_inFlight.insert(path);
        // Higher priority runs first, so negating the distance keeps the
        // queue ordered by nearness even as scrolling appends to it.
        m_pool.start([this, cache, path, size, generation] {
            const QImage image = cache->thumbnail(path, size);
            QMetaObject::invokeMethod(this, [this, path, image, generation] {
                applyThumbnail(path, image, generation);
            }, Qt::QueuedConnection);
        }, -distance);
    }
}

void ThumbnailView::applyThumbnail(const QString &path, const QImage &image,
                                   qint64 generation) {
    // A stale result predates the current grid; its bookkeeping was already
    // discarded when the generation was bumped, so there is nothing to undo.
    if (generation != m_generation) return;
    m_inFlight.remove(path);
    if (image.isNull()) return;

    const int row = m_rowOf.value(path, -1);
    auto *it = row >= 0 ? item(row) : nullptr;
    if (!it) return;

    // Pad to a square iconSize pixmap so every cell is the same size and
    // the icon area in QListView paints predictably.
    QPixmap padded(iconSize());
    padded.setDevicePixelRatio(1.0);
    padded.fill(kPlaceholderColor);
    QPainter p(&padded);
    p.drawPixmap((iconSize().width()  - image.width())  / 2,
                 (iconSize().height() - image.height()) / 2,
                 QPixmap::fromImage(image));
    if (isGifPath(path))
        drawAnimatedBadge(p, iconSize());
    p.end();

    it->setIcon(QIcon(padded));
    m_loaded.insert(path);
}
