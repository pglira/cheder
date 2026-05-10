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
    void returnFocusToView();

    QStackedWidget *m_stack;
    QSplitter *m_thumbSplitter;
    ThumbnailView *m_thumbView;
    InfoPanel *m_infoPanel;
    ImageView *m_imageView;
    ActionPane *m_actionPane;
    QStringList m_files;
    QString m_sourceDir;
    std::unique_ptr<ActionRegistry> m_actions;
    bool m_translating = false;
    bool m_pendingG = false;
};
