#pragma once

#include <QMainWindow>
#include <QStringList>

#include <memory>

class QSplitter;
class QStackedWidget;
class ThumbnailView;
class InfoPanel;
class ImageView;
class Action;
class ActionPane;
class ActionRegistry;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QStringList &files, QWidget *parent = nullptr);
    ~MainWindow() override;

    void showThumbnails();
    void showImage(int index);
    void reload();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    bool inThumbnailView() const;
    bool handleKeyInThumbnails(int key);
    bool handleKeyInImage(int key);
    bool dispatchTranslatedKey(int key, Qt::KeyboardModifiers mods);
    void updateTitle();

    QStringList currentInputs() const;
    QString defaultOutputDirFor(const Action *action) const;
    void runAction(Action *action);
    void deleteCurrentInputs();
    void returnFocusToView();

    QStackedWidget *m_stack;
    ThumbnailView *m_thumbView;
    QSplitter *m_imageSplitter;
    ImageView *m_imageView;
    InfoPanel *m_infoPanel;
    ActionPane *m_actionPane;
    QStringList m_files;
    QString m_sourceDir;
    std::unique_ptr<ActionRegistry> m_actions;
    bool m_translating = false;
    bool m_pendingG = false;
};
