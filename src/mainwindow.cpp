#include "mainwindow.h"

#include "actions/action.h"
#include "actions/actionlogger.h"
#include "actions/actionpane.h"
#include "actions/actionregistry.h"
#include "actions/captionaction.h"
#include "actions/copymoveaction.h"
#include "actions/resizeaction.h"
#include "actions/rotateaction.h"
#include "filelistmodel.h"
#include "imagedir.h"
#include "imageio.h"
#include "imageview.h"
#include "infopanel.h"
#include "thumbnailview.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

MainWindow::MainWindow(const QStringList &files, QWidget *parent)
    : QMainWindow(parent),
      m_fileModel(std::make_unique<FileListModel>(files)),
      m_actions(std::make_unique<ActionRegistry>()) {
    if (!files.isEmpty()) m_sourceDir = QFileInfo(files.first()).absolutePath();

    m_stack = new QStackedWidget(this);

    m_thumbView = new ThumbnailView(m_fileModel.get(), this);
    m_imageView = new ImageView(m_fileModel.get(), this);

    m_infoPanel = new InfoPanel(this);
    m_infoPanel->hide();  // off by default; toggle with `i` while in image view

    m_imageSplitter = new QSplitter(Qt::Horizontal, this);
    m_imageSplitter->addWidget(m_imageView);
    m_imageSplitter->addWidget(m_infoPanel);
    m_imageSplitter->setStretchFactor(0, 1);
    m_imageSplitter->setStretchFactor(1, 0);
    m_imageSplitter->setCollapsible(0, false);
    m_imageSplitter->setSizes({900, 300});

    m_stack->addWidget(m_thumbView);
    m_stack->addWidget(m_imageSplitter);

    statusBar();  // ensures the bar exists for later showMessage() calls

    m_actions->add(std::make_unique<RotateAction>());
    m_actions->add(std::make_unique<ResizeAction>());
    m_actions->add(std::make_unique<CaptionAction>());
    m_actions->add(std::make_unique<CopyMoveAction>());

    m_actionPane = new ActionPane(m_actions.get(), this);

    auto *centralContainer = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(centralContainer);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_stack, 1);
    centralLayout->addWidget(m_actionPane);
    setCentralWidget(centralContainer);

    connect(m_actionPane, &ActionPane::actionInvoked, this, &MainWindow::runAction);
    connect(m_actionPane, &ActionPane::exitRequested, this, &MainWindow::returnFocusToView);

    connect(m_thumbView, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        showImage(m_thumbView->row(it));
    });
    connect(m_imageView, &ImageView::currentChanged, this,
            [this](int, const QString &path) {
                updateTitle();
                // Skip the (potentially exiftool-spawning) refresh if the panel
                // is hidden; the next `i` toggle will load it on demand.
                if (m_infoPanel->isVisible()) m_infoPanel->showFile(path);
            });

    qApp->installEventFilter(this);
    updateTitle();
}

MainWindow::~MainWindow() = default;

void MainWindow::showThumbnails() {
    m_stack->setCurrentWidget(m_thumbView);
    // Sync the thumbnail cursor to the image we were viewing. If nothing's
    // selected yet (e.g. launched on a single file), select that image too;
    // otherwise just move the keyboard cursor without disturbing an existing
    // multi-selection.
    if (m_thumbView->count() > 0) {
        const int idx = m_imageView->index();
        if (idx >= 0 && idx < m_thumbView->count()) {
            const auto selectionCmd = m_thumbView->selectedItems().isEmpty()
                ? QItemSelectionModel::ClearAndSelect
                : QItemSelectionModel::NoUpdate;
            m_thumbView->setCurrentRow(idx, selectionCmd);
        }
    }
    m_thumbView->setFocus();
    updateTitle();
}

void MainWindow::showImage(int index) {
    if (m_fileModel->isEmpty()) return;
    // Switch the stack first so the info panel is on-screen when setIndex()
    // emits currentChanged — otherwise the visibility-gated refresh sees a
    // hidden panel and the panel ends up with stale data from the previous
    // image.
    m_stack->setCurrentWidget(m_imageSplitter);
    m_imageView->setIndex(std::clamp(index, 0, m_fileModel->count() - 1));
    m_imageView->setFocus();
    updateTitle();
}

bool MainWindow::inThumbnailView() const {
    return m_stack->currentWidget() == m_thumbView;
}

void MainWindow::updateTitle() {
    if (!inThumbnailView()) {
        const QString p = m_imageView->currentPath();
        if (!p.isEmpty()) {
            setWindowTitle(QString("%1 (%2/%3) — cheder")
                               .arg(QFileInfo(p).fileName())
                               .arg(m_imageView->index() + 1)
                               .arg(m_fileModel->count()));
            return;
        }
    }
    setWindowTitle(QString("cheder (%1 images)").arg(m_fileModel->count()));
}

// Strict selection: the image-view's current image, or the thumbnail-view's
// explicit multi-selection (no fallback). Empty when nothing is selected.
QStringList MainWindow::selectionPaths() const {
    if (!inThumbnailView()) {
        const QString p = m_imageView->currentPath();
        return p.isEmpty() ? QStringList{} : QStringList{p};
    }
    return m_thumbView->selectedPaths();
}

// Loose selection: same as selectionPaths(), but in thumbnail view falls back
// to the keyboard-focused row when nothing is multi-selected — so Delete and
// run-action behave on "whatever the user is looking at" rather than refusing.
QStringList MainWindow::currentInputs() const {
    QStringList paths = selectionPaths();
    if (paths.isEmpty() && inThumbnailView()) {
        const QString p = m_thumbView->pathAt(m_thumbView->currentRow());
        if (!p.isEmpty()) paths << p;
    }
    return paths;
}

QString MainWindow::defaultOutputDirFor(const Action *action) const {
    Q_UNUSED(action);
    const QStringList inputs = currentInputs();
    if (!inputs.isEmpty()) return QFileInfo(inputs.first()).absolutePath();
    return QDir::currentPath();
}

void MainWindow::runAction(Action *action) {
    if (!action) return;
    const QStringList inputs = currentInputs();
    if (inputs.isEmpty()) {
        statusBar()->showMessage("No image to act on", 3000);
        m_actionPane->resetState();
        returnFocusToView();
        return;
    }
    if (!action->acceptsCount(inputs.size())) {
        statusBar()->showMessage(
            QString("'%1' doesn't accept %2 input(s)").arg(action->name()).arg(inputs.size()), 3000);
        return;
    }

    if (!action->configure(this, inputs, defaultOutputDirFor(action))) {
        m_actionPane->resetState();
        returnFocusToView();
        return;
    }

    const QStringList outputs = action->apply(inputs, m_actionPane->logger());
    if (outputs.isEmpty()) {
        statusBar()->showMessage("Action produced no output", 5000);
    } else {
        const QString outDir = QFileInfo(outputs.first()).absolutePath();
        statusBar()->showMessage(
            QString("Wrote %1 file(s) to %2").arg(outputs.size()).arg(outDir), 7000);
        // Move could have taken files away; in-place edits touch mtimes —
        // re-scan so the views match disk.
        reload();
    }
    m_actionPane->resetState();
    returnFocusToView();
}

void MainWindow::deleteCurrentInputs() {
    const QStringList inputs = currentInputs();
    if (inputs.isEmpty()) {
        statusBar()->showMessage("Nothing to delete", 3000);
        return;
    }

    QString detail;
    const int sampleMax = 5;
    for (int i = 0; i < std::min<int>(inputs.size(), sampleMax); ++i)
        detail += "  " + QFileInfo(inputs[i]).fileName() + "\n";
    if (inputs.size() > sampleMax)
        detail += QString("  ... and %1 more\n").arg(inputs.size() - sampleMax);

    QMessageBox box(this);
    box.setWindowTitle("Delete images");
    box.setIcon(QMessageBox::Warning);
    box.setText(QString("Move %1 image%2 to trash?")
                    .arg(inputs.size()).arg(inputs.size() == 1 ? "" : "s"));
    box.setInformativeText(detail);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;

    auto *logger = m_actionPane->logger();
    if (logger) logger->info(QString("[delete] start — %1 input(s)").arg(inputs.size()));

    int trashed = 0, failed = 0;
    for (const QString &p : inputs) {
        if (QFile::moveToTrash(p)) {
            ++trashed;
            if (logger) logger->info(QString("trashed %1").arg(p));
        } else {
            ++failed;
            if (logger) logger->error(QString("failed to trash %1").arg(p));
        }
    }
    if (logger) logger->info(QString("[delete] done — trashed %1, failed %2")
                                 .arg(trashed).arg(failed));

    statusBar()->showMessage(
        QString("Trashed %1 of %2 image(s)").arg(trashed).arg(inputs.size()), 5000);
    reload();
}

void MainWindow::copySelectionToClipboard() {
    // Strict selection — unlike Delete and run-action (which use currentInputs()
    // and fall back to the focused row), Ctrl+C after deselecting reports
    // "Nothing to copy" rather than silently grabbing the focused thumbnail.
    const QStringList inputs = selectionPaths();
    if (inputs.isEmpty()) {
        statusBar()->showMessage("Nothing to copy", 3000);
        return;
    }

    auto *mime = new QMimeData;
    QList<QUrl> urls;
    urls.reserve(inputs.size());
    for (const QString &p : inputs) urls << QUrl::fromLocalFile(p);
    mime->setUrls(urls);

    QString message;
    if (inputs.size() == 1) {
        const QString name = QFileInfo(inputs.first()).fileName();
        const QImage img = readImage(inputs.first());
        if (!img.isNull()) {
            mime->setImageData(img);
            message = QString("Copied %1 to clipboard").arg(name);
        } else {
            message = QString("Copied %1 to clipboard — image decode failed").arg(name);
        }
    } else {
        message = QString("Copied %1 images to clipboard").arg(inputs.size());
    }

    QApplication::clipboard()->setMimeData(mime);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::returnFocusToView() {
    if (inThumbnailView()) m_thumbView->setFocus();
    else                   m_imageView->setFocus();
}

void MainWindow::reload() {
    if (m_sourceDir.isEmpty()) return;

    // FileListModel emits filesChanged; ImageView preserves its current
    // image by path (or clamps the index if the file is gone) and
    // ThumbnailView preserves selection.
    m_fileModel->setFiles(listImagesInDir(m_sourceDir));

    statusBar()->showMessage(QString("Reloaded — %1 image(s)").arg(m_fileModel->count()), 2500);
    updateTitle();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    Q_UNUSED(obj);
    if (m_dispatchingSyntheticKey) return false;
    if (event->type() != QEvent::KeyPress || !isActiveWindow()) return false;

    auto *ke = static_cast<QKeyEvent *>(event);
    const int origKey = ke->key();
    const auto origMods = ke->modifiers();

    // F5 reloads from any context (including while typing in the action bar).
    if (origKey == Qt::Key_F5) {
        reload();
        return true;
    }

    // Ctrl+P focuses the action bar from any view. Handled before the
    // input-focused guard so it also re-selects the field when already
    // focused (harmless no-op visually).
    if (origKey == Qt::Key_P && (origMods & Qt::ControlModifier)) {
        m_actionPane->focusInput();
        return true;
    }

    // While the user is typing in the action bar's search field, leave the
    // event alone — no vim translation, no view shortcuts.
    if (m_actionPane->isInputFocused()) return false;

    // Ctrl+C copies the current selection. Placed after the input-focused
    // guard so the action bar's line edit keeps its native text-copy.
    if (origKey == Qt::Key_C && (origMods & Qt::ControlModifier)) {
        copySelectionToClipboard();
        return true;
    }

    // 'q' (or Shift+Q) closes the window from either view; placed after the
    // input-focused guard so typing 'q' into the filter just types a letter.
    if (origKey == Qt::Key_Q
        && !(origMods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        close();
        return true;
    }

    // "gg" sequence: lowercase g pressed twice -> Home
    if (origKey == Qt::Key_G && !(origMods & Qt::ShiftModifier)) {
        m_pendingD = false;  // 'g' cancels a pending 'd'
        if (!m_pendingG) {
            m_pendingG = true;
            return true;
        }
        m_pendingG = false;
        return dispatchTranslatedKey(Qt::Key_Home, origMods);
    }

    // "dd" sequence (vim-style): lowercase d pressed twice -> delete inputs.
    if (origKey == Qt::Key_D
        && !(origMods & (Qt::ShiftModifier | Qt::ControlModifier
                         | Qt::AltModifier | Qt::MetaModifier))) {
        m_pendingG = false;  // 'd' cancels a pending 'g'
        if (!m_pendingD) {
            m_pendingD = true;
            return true;
        }
        m_pendingD = false;
        deleteCurrentInputs();
        return true;
    }

    // Any other key cancels both pending sequences.
    m_pendingG = false;
    m_pendingD = false;

    int key = origKey;
    auto mods = origMods;
    switch (origKey) {
    case Qt::Key_H: key = Qt::Key_Left;  break;
    case Qt::Key_J: key = Qt::Key_Down;  break;
    case Qt::Key_K: key = Qt::Key_Up;    break;
    case Qt::Key_L: key = Qt::Key_Right; break;
    case Qt::Key_G:
        // Shift+G -> End. Drop the Shift used to type the capital.
        key = Qt::Key_End;
        mods &= ~Qt::ShiftModifier;
        break;
    }

    if (key != origKey)
        return dispatchTranslatedKey(key, mods);

    return inThumbnailView() ? handleKeyInThumbnails(origKey) : handleKeyInImage(origKey);
}

bool MainWindow::dispatchTranslatedKey(int key, Qt::KeyboardModifiers mods) {
    if (inThumbnailView() ? handleKeyInThumbnails(key) : handleKeyInImage(key))
        return true;
    // No explicit handler — re-dispatch as a synthetic event so the focused
    // widget sees the translated key (e.g. QListWidget's built-in nav).
    if (QWidget *target = QApplication::focusWidget()) {
        m_dispatchingSyntheticKey = true;
        QKeyEvent translated(QEvent::KeyPress, key, mods);
        QApplication::sendEvent(target, &translated);
        m_dispatchingSyntheticKey = false;
    }
    return true;
}

bool MainWindow::handleKeyInThumbnails(int key) {
    switch (key) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        int row = m_thumbView->firstSelectedRow();
        if (row < 0) row = 0;
        showImage(row);
        return true;
    }
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        m_thumbView->zoomIn();
        return true;
    case Qt::Key_Minus:
        m_thumbView->zoomOut();
        return true;
    case Qt::Key_Delete:
        deleteCurrentInputs();
        return true;
    }
    return false;
}

bool MainWindow::handleKeyInImage(int key) {
    switch (key) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Escape:
        showThumbnails();
        return true;
    case Qt::Key_Right:
    case Qt::Key_N:
        m_imageView->next();
        return true;
    case Qt::Key_Left:
    case Qt::Key_P:
        m_imageView->previous();
        return true;
    case Qt::Key_I:
        m_infoPanel->setVisible(!m_infoPanel->isVisible());
        // Hidden -> shown: panel may be stale (we skipped updates while hidden).
        // Refresh against the current image so it lights up immediately.
        if (m_infoPanel->isVisible())
            m_infoPanel->showFile(m_imageView->currentPath());
        return true;
    case Qt::Key_Delete:
        deleteCurrentInputs();
        return true;
    }
    return false;
}
