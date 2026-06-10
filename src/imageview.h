#pragma once

#include <QPixmap>
#include <QPointF>
#include <QSize>
#include <QWidget>

#include <memory>

class QLabel;
class QMovie;
class QTimer;
class FileListModel;

class ImageView : public QWidget {
    Q_OBJECT
public:
    explicit ImageView(FileListModel *model, QWidget *parent = nullptr);
    ~ImageView() override;  // out-of-line for std::unique_ptr<QMovie> forward decl

    void setIndex(int index);
    int  index() const { return m_index; }
    QString currentPath() const { return m_currentPath; }

    void next();
    void previous();

    // Zoom/pan (stills only; no-ops while a GIF is playing). Zoom is the
    // display scale relative to actual pixels (1.0 = 100%), clamped to
    // [minScale(), maxScale()] — nominally [fitScale/4, 32], widened where
    // needed so 100% and fit itself are always reachable. Fit-to-window is a
    // sticky mode, not a number: while m_fitMode is set, resizes re-fit
    // instead of preserving the scale.
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToActual();
    // Moves the viewport by (sx, sy) steps over the image (one step = 1/8 of
    // the viewport dimension). panBy(-1, 0) scrolls the view left.
    void panBy(int sx, int sy);
    // Switches rendering above 100% between bilinear (default) and
    // nearest-neighbor. Below 100% rendering is always smooth.
    void toggleInterpolation();

signals:
    void currentChanged(int index, const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void onFilesChanged();
    void loadCurrent();
    void showLoadError(const QString &text);
    void applyMovieScale();

    bool   hasStill() const { return !m_movie && !m_original.isNull(); }
    double fitScale() const;
    double minScale() const;
    double maxScale() const;
    void   applyFit();
    void   applyScale(double scale, QPointF anchor);
    void   zoomTo(double scale, QPointF anchor);
    void   clampOffset();
    QSize  downscaleTarget() const;
    void   regenerateDownscaleCache();
    void   showBadge(const QString &text);
    void   showZoomBadge();
    void   positionBadge();

    QLabel *m_label;        // GIF playback and error text; hidden for stills
    FileListModel *m_model;
    int m_index = 0;
    QString m_currentPath;  // path for the image at m_index, "" if none
    QPixmap m_original;     // populated only when current path is a still image
    std::unique_ptr<QMovie> m_movie;  // non-null only while current path is a GIF
    QSize m_movieNativeSize;          // peeked once at load; QMovie can't report it early

    bool    m_fitMode = true;
    bool    m_nearest = false;     // session-wide; survives image changes
    double  m_scale   = 1.0;       // display pixels per image pixel
    QPointF m_offset;              // widget coords of the image's top-left
    QPixmap m_downscaleCache;      // smooth-scaled copy used while m_scale < 1
    QTimer *m_cacheTimer;          // debounces cache regeneration while zooming

    bool   m_dragging = false;
    QPoint m_lastDragPos;

    QLabel *m_zoomBadge;
    QTimer *m_badgeTimer;
};
