#pragma once

#include <QListWidget>

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

private:
    void onFilesChanged();
    void rebuildItems();
    void startLoading();
    void loadNext(qint64 generation);

    FileListModel *m_model;
    int m_loadIndex = 0;
    qint64 m_generation = 0;
    std::unique_ptr<ThumbnailCache> m_cache;
};
