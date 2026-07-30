#pragma once

#include <QHash>
#include <QListWidget>
#include <QPixmap>
#include <QSet>
#include <QThreadPool>
#include <QTimer>

#include <memory>

class ThumbnailCache;
class FileListModel;

class ThumbnailView : public QListWidget {
    Q_OBJECT
public:
    explicit ThumbnailView(FileListModel *model, QWidget *parent = nullptr);
    ~ThumbnailView() override;

    int firstSelectedRow() const;

    // Path of the item at `row` (empty if out of range). Reads the UserRole,
    // which is set when items are built.
    QString pathAt(int row) const;
    // Paths of the currently selected items, in row order. Empty if nothing
    // is selected.
    QStringList selectedPaths() const;

    int thumbnailSize() const;
    void setThumbnailSize(int size);
    void zoomIn();
    void zoomOut();

    // Exposed so peer widgets (e.g. InfoPanel) can render previews using the
    // same disk cache instead of duplicating one.
    ThumbnailCache *cache() const { return m_cache.get(); }

protected:
    void wheelEvent(QWheelEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    void onFilesChanged();
    void rebuildItems();
    // Queue a pass over the viewport, coalescing the bursts of requests that
    // scrolling and resizing produce into one.
    void scheduleThumbnailGeneration();
    // Dispatch decode jobs for the items in and near the viewport, nearest
    // first, and release thumbnails that have scrolled far out of it.
    void generateThumbnailsForItems();
    // Paint a finished decode into its item's icon. Results carrying a stale
    // generation are dropped: the file list or icon size moved on while they
    // were in flight.
    void applyThumbnail(const QString &path, const QImage &image, qint64 generation);

    FileListModel *m_model;
    qint64 m_generation = 0;
    QPixmap m_placeholder;
    QTimer m_scheduleTimer;
    // Decoding runs here rather than on the GUI thread, which is what keeps
    // the grid responsive while a directory is filling in.
    QThreadPool m_pool;
    QHash<QString, int> m_rowOf;
    QSet<QString> m_loaded;    // paths whose icon already holds a thumbnail
    QSet<QString> m_inFlight;  // paths handed to the pool, not yet applied
    std::unique_ptr<ThumbnailCache> m_cache;
};
