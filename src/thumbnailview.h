#pragma once

#include <QListWidget>
#include <QStringList>

#include <memory>

class ThumbnailCache;

class ThumbnailView : public QListWidget {
    Q_OBJECT
public:
    explicit ThumbnailView(QWidget *parent = nullptr);
    ~ThumbnailView() override;

    void setFiles(const QStringList &files);
    int firstSelectedRow() const;

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
    void rebuildItems();
    void startLoading();
    void loadNext(qint64 generation);

    QStringList m_files;
    int m_loadIndex = 0;
    qint64 m_generation = 0;
    std::unique_ptr<ThumbnailCache> m_cache;
};
