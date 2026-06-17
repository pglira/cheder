#pragma once

#include "keydispatcher.h"

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
class FileListModel;

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
    void updateTitle();
    void wireKeyBindings();

    QStringList selectionPaths() const;
    QStringList currentInputs() const;
    QString defaultOutputDirFor(const Action *action) const;
    // Makes `path` the active file in whichever view is showing, selecting it
    // (and clearing any other selection) in the thumbnail view. No-op if the
    // path isn't in the current listing.
    void activatePath(const QString &path);
    void runAction(Action *action);
    void deleteCurrentInputs();
    void copySelectionImagesToClipboard();
    void copySelectionPathsToClipboard();
    void sendSelectionToDungeon();
    void returnFocusToView();

    QStackedWidget *m_stack;
    ThumbnailView *m_thumbView;
    QSplitter *m_imageSplitter;
    ImageView *m_imageView;
    InfoPanel *m_infoPanel;
    ActionPane *m_actionPane;
    std::unique_ptr<FileListModel> m_fileModel;
    QString m_sourceDir;
    std::unique_ptr<ActionRegistry> m_actions;
    KeyDispatcher m_keys;
    bool m_dispatchingSyntheticKey = false;
};
