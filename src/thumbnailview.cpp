#include "thumbnailview.h"

#include "thumbnailcache.h"

#include <QFileInfo>
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

}  // namespace

ThumbnailView::ThumbnailView(QWidget *parent)
    : QListWidget(parent), m_cache(std::make_unique<ThumbnailCache>()) {
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

void ThumbnailView::setFiles(const QStringList &files) {
    m_files = files;
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

void ThumbnailView::rebuildItems() {
    QSet<QString> previouslySelected;
    for (auto *it : selectedItems()) previouslySelected.insert(it->toolTip());
    const int prevCurrentRow = currentRow();

    clear();
    const QPixmap placeholder = placeholderPixmap(iconSize());
    for (const QString &path : m_files) {
        auto *item = new QListWidgetItem(QIcon(placeholder), QString(), this);
        item->setToolTip(path);
        item->setSizeHint(gridSize());
        if (previouslySelected.contains(path)) item->setSelected(true);
    }
    if (prevCurrentRow >= 0 && prevCurrentRow < count())
        setCurrentRow(prevCurrentRow);
}

void ThumbnailView::startLoading() {
    ++m_generation;
    m_loadIndex = 0;
    const qint64 gen = m_generation;
    QTimer::singleShot(0, this, [this, gen] { loadNext(gen); });
}

void ThumbnailView::loadNext(qint64 generation) {
    if (generation != m_generation) return;
    if (m_loadIndex >= m_files.size()) return;

    const int row = m_loadIndex++;
    const QPixmap raw = m_cache->getThumbnail(m_files.at(row), iconSize());
    if (generation == m_generation && !raw.isNull()) {
        // Pad to a square iconSize pixmap so every cell is the same size and
        // the icon area in QListView paints predictably.
        QPixmap padded(iconSize());
        padded.setDevicePixelRatio(1.0);
        padded.fill(kPlaceholderColor);
        QPainter p(&padded);
        p.drawPixmap((iconSize().width()  - raw.width())  / 2,
                     (iconSize().height() - raw.height()) / 2, raw);
        p.end();
        if (auto *it = item(row)) it->setIcon(QIcon(padded));
    }

    QTimer::singleShot(0, this, [this, generation] { loadNext(generation); });
}
