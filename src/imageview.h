#pragma once

#include <QPixmap>
#include <QWidget>

class QLabel;
class FileListModel;

class ImageView : public QWidget {
    Q_OBJECT
public:
    explicit ImageView(FileListModel *model, QWidget *parent = nullptr);

    void setIndex(int index);
    int  index() const { return m_index; }
    QString currentPath() const;

    void next();
    void previous();

signals:
    void currentChanged(int index, const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void onFilesChanged();
    void loadCurrent();
    void updatePixmap();

    QLabel *m_label;
    FileListModel *m_model;
    int m_index = 0;
    QPixmap m_original;
};
