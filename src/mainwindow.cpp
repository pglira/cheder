#include "mainwindow.h"

#include "actions/action.h"
#include "actions/actionlogger.h"
#include "actions/actionpane.h"
#include "actions/actionregistry.h"
#include "actions/copymoveaction.h"
#include "actions/resizeaction.h"
#include "actions/rotateaction.h"
#include "imagedir.h"
#include "imageview.h"
#include "infopanel.h"
#include "thumbnailview.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>

MainWindow::MainWindow(const QStringList &files, QWidget *parent)
    : QMainWindow(parent), m_files(files), m_actions(std::make_unique<ActionRegistry>()) {
    if (!files.isEmpty()) m_sourceDir = QFileInfo(files.first()).absolutePath();

    m_stack = new QStackedWidget(this);

    m_thumbView = new ThumbnailView(this);
    m_thumbView->setFiles(files);

    m_infoPanel = new InfoPanel(m_thumbView->cache(), this);

    m_thumbSplitter = new QSplitter(Qt::Horizontal, this);
    m_thumbSplitter->addWidget(m_thumbView);
    m_thumbSplitter->addWidget(m_infoPanel);
    m_thumbSplitter->setStretchFactor(0, 1);
    m_thumbSplitter->setStretchFactor(1, 0);
    m_thumbSplitter->setCollapsible(0, false);
    m_thumbSplitter->setSizes({800, 280});

    m_imageView = new ImageView(this);
    m_imageView->setFiles(files);

    m_stack->addWidget(m_thumbSplitter);
    m_stack->addWidget(m_imageView);

    statusBar();  // ensures the bar exists for later showMessage() calls

    m_actions->add(std::make_unique<RotateAction>());
    m_actions->add(std::make_unique<ResizeAction>());
    m_actions->add(std::make_unique<CopyMoveAction>());

    m_actionPane = new ActionPane(m_actions.get(), this);

    auto *centralContainer = new QWidget(this);
    auto *vlay = new QVBoxLayout(centralContainer);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);
    vlay->addWidget(m_stack, 1);
    vlay->addWidget(m_actionPane);
    setCentralWidget(centralContainer);

    connect(m_actionPane, &ActionPane::actionInvoked, this, &MainWindow::runAction);
    connect(m_actionPane, &ActionPane::exitRequested, this, &MainWindow::returnFocusToView);

    connect(m_thumbView, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        showImage(m_thumbView->row(it));
    });
    connect(m_thumbView, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur, QListWidgetItem *) {
                m_infoPanel->showFile(cur ? cur->toolTip() : QString());
            });
    connect(m_imageView, &ImageView::currentChanged, this, [this] { updateTitle(); });

    qApp->installEventFilter(this);
    updateTitle();
}

MainWindow::~MainWindow() = default;

void MainWindow::showThumbnails() {
    m_stack->setCurrentWidget(m_thumbSplitter);
    m_thumbView->setFocus();
    updateTitle();
}

void MainWindow::showImage(int index) {
    if (m_files.isEmpty()) return;
    m_imageView->setIndex(std::clamp<int>(index, 0, static_cast<int>(m_files.size()) - 1));
    m_stack->setCurrentWidget(m_imageView);
    m_imageView->setFocus();
    updateTitle();
}

bool MainWindow::inThumbnailView() const {
    return m_stack->currentWidget() == m_thumbSplitter;
}

void MainWindow::updateTitle() {
    if (!inThumbnailView()) {
        const QString p = m_imageView->currentPath();
        if (!p.isEmpty()) {
            setWindowTitle(QString("%1 (%2/%3) — cheder")
                               .arg(QFileInfo(p).fileName())
                               .arg(m_imageView->index() + 1)
                               .arg(m_files.size()));
            return;
        }
    }
    setWindowTitle(QString("cheder (%1 images)").arg(m_files.size()));
}

QStringList MainWindow::currentInputs() const {
    if (!inThumbnailView()) {
        const QString p = m_imageView->currentPath();
        return p.isEmpty() ? QStringList{} : QStringList{p};
    }
    QStringList paths;
    for (auto *it : m_thumbView->selectedItems()) paths << it->toolTip();
    if (paths.isEmpty()) {
        if (auto *it = m_thumbView->currentItem()) paths << it->toolTip();
    }
    return paths;
}

QString MainWindow::defaultOutputDirFor(const Action *action) const {
    const QStringList inputs = currentInputs();
    QString srcDir;
    if (!inputs.isEmpty()) srcDir = QFileInfo(inputs.first()).absolutePath();
    else                   srcDir = QDir::currentPath();
    return srcDir + '/' + action->id();
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

void MainWindow::returnFocusToView() {
    if (inThumbnailView()) m_thumbView->setFocus();
    else                   m_imageView->setFocus();
}

void MainWindow::reload() {
    if (m_sourceDir.isEmpty()) return;

    const QString currentImagePath = m_imageView->currentPath();

    m_files = listImagesInDir(m_sourceDir);
    // ThumbnailView::setFiles preserves selection by tooltip path; ImageView
    // clamps the index, so we re-locate the previous image afterwards.
    m_thumbView->setFiles(m_files);
    m_imageView->setFiles(m_files);

    if (!currentImagePath.isEmpty()) {
        const int idx = m_files.indexOf(currentImagePath);
        if (idx >= 0) m_imageView->setIndex(idx);
    }

    statusBar()->showMessage(QString("Reloaded — %1 image(s)").arg(m_files.size()), 2500);
    updateTitle();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    Q_UNUSED(obj);
    if (m_translating) return false;
    if (event->type() != QEvent::KeyPress || !isActiveWindow()) return false;

    auto *ke = static_cast<QKeyEvent *>(event);
    const int origKey = ke->key();
    const auto origMods = ke->modifiers();

    // F5 reloads from any context (including while typing in the action bar).
    if (origKey == Qt::Key_F5) {
        reload();
        return true;
    }

    // While the user is typing in the action bar's search field, leave the
    // event alone — no vim translation, no view shortcuts (so `:` types
    // a colon into the field instead of re-focusing it).
    if (m_actionPane->isInputFocused()) return false;

    // ':' (vim-style command activation) focuses the action bar from any view.
    if (origKey == Qt::Key_Colon) {
        m_actionPane->focusInput();
        return true;
    }

    // "gg" sequence: lowercase g pressed twice -> Home
    if (origKey == Qt::Key_G && !(origMods & Qt::ShiftModifier)) {
        if (!m_pendingG) {
            m_pendingG = true;
            return true;
        }
        m_pendingG = false;
        return dispatchTranslatedKey(Qt::Key_Home, origMods);
    }
    m_pendingG = false;  // any other key cancels pending g

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
        m_translating = true;
        QKeyEvent translated(QEvent::KeyPress, key, mods);
        QApplication::sendEvent(target, &translated);
        m_translating = false;
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
    case Qt::Key_I:
        m_infoPanel->setVisible(!m_infoPanel->isVisible());
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
    }
    return false;
}
