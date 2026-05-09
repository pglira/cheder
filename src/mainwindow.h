#pragma once

#include <QMainWindow>
#include <QStringList>

class QStackedWidget;
class ThumbnailView;
class ImageView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QStringList &files, QWidget *parent = nullptr);

    void showThumbnails();
    void showImage(int index);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    bool inThumbnailView() const;
    bool handleKeyInThumbnails(int key);
    bool handleKeyInImage(int key);
    bool dispatchTranslatedKey(int key, Qt::KeyboardModifiers mods);
    void updateTitle();

    QStackedWidget *m_stack;
    ThumbnailView *m_thumbView;
    ImageView *m_imageView;
    QStringList m_files;
    bool m_translating = false;
    bool m_pendingG = false;
};
