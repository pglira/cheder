#pragma once

#include <QPixmap>
#include <QWidget>

#include <memory>

class QLabel;
class QMovie;
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

signals:
    void currentChanged(int index, const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void onFilesChanged();
    void loadCurrent();
    void rescale();
    void applyMovieScale();

    QLabel *m_label;
    FileListModel *m_model;
    int m_index = 0;
    QString m_currentPath;  // path for the image at m_index, "" if none
    QPixmap m_original;     // populated only when current path is a still image
    std::unique_ptr<QMovie> m_movie;  // non-null only while current path is a GIF
};
