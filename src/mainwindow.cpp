#include "mainwindow.h"

#include "imageview.h"
#include "thumbnailview.h"

#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QStackedWidget>

#include <algorithm>

MainWindow::MainWindow(const QStringList &files, QWidget *parent)
    : QMainWindow(parent), m_files(files) {
    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    m_thumbView = new ThumbnailView(this);
    m_thumbView->setFiles(files);

    m_imageView = new ImageView(this);
    m_imageView->setFiles(files);

    m_stack->addWidget(m_thumbView);
    m_stack->addWidget(m_imageView);

    connect(m_thumbView, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        showImage(m_thumbView->row(it));
    });
    connect(m_imageView, &ImageView::currentChanged, this, [this] { updateTitle(); });

    qApp->installEventFilter(this);
    updateTitle();
}

void MainWindow::showThumbnails() {
    m_stack->setCurrentWidget(m_thumbView);
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
    return m_stack->currentWidget() == m_thumbView;
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

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    Q_UNUSED(obj);
    if (m_translating) return false;
    if (event->type() != QEvent::KeyPress || !isActiveWindow()) return false;

    auto *ke = static_cast<QKeyEvent *>(event);
    const int origKey = ke->key();
    const auto origMods = ke->modifiers();

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
