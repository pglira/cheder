#include "thumbnailview.h"

#include "filelistmodel.h"
#include "imageio.h"
#include "thumbnailcache.h"

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>

namespace {

constexpr int kDefaultThumbSize = 128;
constexpr int kThumbSizeStep    = 32;
constexpr int kThumbSizeMin     = 64;
constexpr int kThumbSizeMax     = 512;
constexpr int kGridPaddingW     = 8;
constexpr int kGridPaddingH     = 8;

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

    connect(m_model, &FileListModel::filesChanged, this, &ThumbnailView::onFilesChanged);
    onFilesChanged();
}

ThumbnailView::~ThumbnailView() = default;

int ThumbnailView::thumbnailSize() const {
    return iconSize().width();
}

void ThumbnailView::setThumbnailSize(int size) {
    size = std::clamp(size, kThumbSizeMin, kThumbSizeMax);
    if (size == thumbnailSize()) return;
    setIconSize(QSize(size, size));
    setGridSize(gridSizeFor(size));
    rebuildItems();
    startLoading();
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
    startLoading();
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
    // reason). Fall back to clamping the old row only if the path is gone.
    const QString prevCurrentPath =
        currentItem() ? currentItem()->data(Qt::UserRole).toString() : QString();
    const int prevCurrentRow = currentRow();

    clear();
    const QPixmap placeholder = placeholderPixmap(iconSize());
    int currentRowToRestore = -1;
    const QStringList &files = m_model->files();
    for (int i = 0; i < files.size(); ++i) {
        const QString &path = files.at(i);
        auto *item = new QListWidgetItem(QIcon(placeholder), QString(), this);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        item->setSizeHint(gridSize());
        if (previouslySelected.contains(path)) item->setSelected(true);
        if (path == prevCurrentPath) currentRowToRestore = i;
    }
    if (currentRowToRestore < 0 && prevCurrentRow >= 0 && prevCurrentRow < count())
        currentRowToRestore = prevCurrentRow;
    if (currentRowToRestore >= 0)
        setCurrentRow(currentRowToRestore);
}

void ThumbnailView::startLoading() {
    ++m_generation;
    m_loadIndex = 0;
    const qint64 gen = m_generation;
    QTimer::singleShot(0, this, [this, gen] { loadNext(gen); });
}

void ThumbnailView::loadNext(qint64 generation) {
    if (generation != m_generation) return;
    const QStringList &files = m_model->files();
    if (m_loadIndex >= files.size()) return;

    const int row = m_loadIndex++;
    const QString &path = files.at(row);
    const QPixmap raw = m_cache->getThumbnail(path, iconSize());
    if (generation == m_generation && !raw.isNull()) {
        // Pad to a square iconSize pixmap so every cell is the same size and
        // the icon area in QListView paints predictably.
        QPixmap padded(iconSize());
        padded.setDevicePixelRatio(1.0);
        padded.fill(kPlaceholderColor);
        QPainter p(&padded);
        p.drawPixmap((iconSize().width()  - raw.width())  / 2,
                     (iconSize().height() - raw.height()) / 2, raw);
        if (isGifPath(path))
            drawAnimatedBadge(p, iconSize());
        p.end();
        if (auto *it = item(row)) it->setIcon(QIcon(padded));
    }

    QTimer::singleShot(0, this, [this, generation] { loadNext(generation); });
}
