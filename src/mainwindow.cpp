#include "mainwindow.h"

#include "actions/action.h"
#include "actions/actionlogger.h"
#include "actions/actionpane.h"
#include "actions/actionregistry.h"
#include "actions/animationaction.h"
#include "actions/annotateaction.h"
#include "actions/captionaction.h"
#include "actions/concatenateaction.h"
#include "actions/copymoveaction.h"
#include "actions/cropaction.h"
#include "actions/gridaction.h"
#include "actions/renameaction.h"
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
#include <QProcess>
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
    m_actions->add(std::make_unique<AnimationAction>());
    m_actions->add(std::make_unique<AnnotateAction>());
    m_actions->add(std::make_unique<CaptionAction>());
    m_actions->add(std::make_unique<CropAction>());
    m_actions->add(std::make_unique<ConcatenateAction>());
    m_actions->add(std::make_unique<GridAction>());
    m_actions->add(std::make_unique<RenameAction>());
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
                // InfoPanel gates its own refresh on visibility — safe to call
                // unconditionally; hidden updates are deferred until shown.
                m_infoPanel->setCurrentPath(path);
            });

    wireKeyBindings();

    qApp->installEventFilter(this);
    updateTitle();
}

void MainWindow::wireKeyBindings() {
    using Mode = KeyDispatcher::Mode;

    // Global shortcuts that fire even while the action bar's input is focused.
    m_keys.bind({Qt::Key_F5,  {}, {}, Mode::Anywhere, /*fireWhileInputFocused=*/true,
                 [this] { reload(); }});
    m_keys.bind({Qt::Key_F9,  {}, {}, Mode::Anywhere, /*fireWhileInputFocused=*/true,
                 [this] { copySelectionPathsToClipboard(); }});
    m_keys.bind({Qt::Key_P, Qt::ControlModifier, {}, Mode::Anywhere, true,
                 [this] { m_actionPane->focusInput(); }});

    // 'q' / 'Q' closes from any view; Ctrl/Alt/Meta+Q does not (let the OS or
    // the action bar's QLineEdit handle those).
    m_keys.bind({Qt::Key_Q, {},
                 Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier,
                 Mode::Anywhere, false, [this] { close(); }});

    m_keys.bind({Qt::Key_C, Qt::ControlModifier, {}, Mode::Anywhere, false,
                 [this] { copySelectionImagesToClipboard(); }});

    // Plain 'm' (no modifier) sends the current selection to dungeon. The
    // forbidden-modifier set excludes capital M (Shift) and combos like
    // Alt+M (reserved for the Copy or Move action) so this binding only
    // fires for unmodified, lowercase 'm'. fireWhileInputFocused=false
    // lets the action-bar filter still receive 'm' as a typed letter.
    m_keys.bind({Qt::Key_M, {},
                 Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier | Qt::ShiftModifier,
                 Mode::Anywhere, false,
                 [this] { sendSelectionToDungeon(); }});

    // Vim sequences: gg → Home (via synthetic dispatch to focused widget);
    // dd → delete current selection (with confirmation).
    m_keys.setSequenceTranslation(Qt::Key_G, Qt::Key_Home);
    m_keys.setSequenceHandler(Qt::Key_D, [this] { deleteCurrentInputs(); });

    // Vim translations: hjkl → arrow keys (regardless of modifiers);
    // Shift+G → End (Shift stripped from the synthesized event).
    m_keys.addTranslation({Qt::Key_H, {}, Qt::Key_Left,  {}});
    m_keys.addTranslation({Qt::Key_J, {}, Qt::Key_Down,  {}});
    m_keys.addTranslation({Qt::Key_K, {}, Qt::Key_Up,    {}});
    m_keys.addTranslation({Qt::Key_L, {}, Qt::Key_Right, {}});
    m_keys.addTranslation({Qt::Key_G, Qt::ShiftModifier, Qt::Key_End, Qt::ShiftModifier});

    // Delete from either view (with confirmation).
    m_keys.bind({Qt::Key_Delete, {}, {}, Mode::Anywhere, false,
                 [this] { deleteCurrentInputs(); }});

    // Thumbnail view: open / zoom.
    auto bindThumb = [this](Qt::Key k, KeyDispatcher::Handler h) {
        m_keys.bind({k, {}, {}, Mode::Thumbnail, false, std::move(h)});
    };
    auto openCurrent = [this] {
        int row = m_thumbView->firstSelectedRow();
        if (row < 0) row = 0;
        showImage(row);
    };
    bindThumb(Qt::Key_Tab,    openCurrent);
    bindThumb(Qt::Key_Backtab, openCurrent);
    bindThumb(Qt::Key_Return,  openCurrent);
    bindThumb(Qt::Key_Enter,   openCurrent);
    bindThumb(Qt::Key_Plus,    [this] { m_thumbView->zoomIn();  });
    bindThumb(Qt::Key_Equal,   [this] { m_thumbView->zoomIn();  });
    bindThumb(Qt::Key_Minus,   [this] { m_thumbView->zoomOut(); });

    // Image view: nav / info-panel toggle / back to thumbnails.
    auto bindImage = [this](Qt::Key k, KeyDispatcher::Handler h) {
        m_keys.bind({k, {}, {}, Mode::Image, false, std::move(h)});
    };
    auto back = [this] { showThumbnails(); };
    bindImage(Qt::Key_Tab,    back);
    bindImage(Qt::Key_Backtab, back);
    bindImage(Qt::Key_Escape,  back);
    bindImage(Qt::Key_Right, [this] { m_imageView->next();     });
    bindImage(Qt::Key_N,     [this] { m_imageView->next();     });
    bindImage(Qt::Key_Left,  [this] { m_imageView->previous(); });
    bindImage(Qt::Key_P,     [this] { m_imageView->previous(); });
    bindImage(Qt::Key_I,     [this] {
        m_infoPanel->setVisible(!m_infoPanel->isVisible());
    });

    // Each Action can declare its own QKeySequence (Alt+R, Alt+S, etc.).
    // Wire it to runAction(); runAction's existing acceptsCount check handles
    // the wrong-input-count case with a status message, so shortcuts at the
    // wrong selection size fail soft rather than firing the dialog.
    for (Action *a : m_actions->all()) {
        const QKeySequence seq = a->shortcut();
        if (seq.isEmpty()) continue;
        const auto combo = seq[0];
        m_keys.bind({combo.key(), combo.keyboardModifiers(), {}, Mode::Anywhere,
                     /*fireWhileInputFocused=*/true,
                     [this, a] { runAction(a); }});
    }
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

void MainWindow::copySelectionPathsToClipboard() {
    // Mirrors copySelectionImagesToClipboard's strict selection model — F9 with
    // nothing multi-selected gives "Nothing to copy" rather than grabbing
    // the focused row.
    //
    // Payload is plain text (one absolute path per line) — no text/uri-list.
    // Browsers prefer text/uri-list when both are present and then refuse
    // the file:// URI for security, which breaks address-bar pasting. F9 is
    // the "give me text I can paste anywhere" shortcut; Ctrl+C remains the
    // rich-payload shortcut (pixels + uri-list) for image content.
    const QStringList inputs = selectionPaths();
    if (inputs.isEmpty()) {
        statusBar()->showMessage("Nothing to copy", 3000);
        return;
    }

    const QString message = inputs.size() == 1
        ? QString("Copied %1 path to clipboard").arg(QFileInfo(inputs.first()).fileName())
        : QString("Copied %1 paths to clipboard").arg(inputs.size());

    QApplication::clipboard()->setText(inputs.join('\n'));
    statusBar()->showMessage(message, 5000);
}

void MainWindow::sendSelectionToDungeon() {
    // Hand the current selection to the external `dungeon` helper as command-
    // line arguments. Strict-selection semantics like F9: no fallback to the
    // focused row, so the user always sees what they're sending. startDetached
    // returns immediately and lets dungeon outlive the cheder process.
    const QStringList inputs = selectionPaths();
    if (inputs.isEmpty()) {
        statusBar()->showMessage("Nothing to send to dungeon", 3000);
        return;
    }

    if (!QProcess::startDetached("dungeon", inputs)) {
        statusBar()->showMessage("Failed to start dungeon (is it on PATH?)", 5000);
        return;
    }

    const QString message = inputs.size() == 1
        ? QString("Sent %1 to dungeon").arg(QFileInfo(inputs.first()).fileName())
        : QString("Sent %1 files to dungeon").arg(inputs.size());
    statusBar()->showMessage(message, 3000);
}

void MainWindow::copySelectionImagesToClipboard() {
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
    const auto mode = inThumbnailView()
        ? KeyDispatcher::Mode::Thumbnail
        : KeyDispatcher::Mode::Image;
    const auto r = m_keys.dispatch(ke, mode, m_actionPane->isInputFocused());

    switch (r.status) {
    case KeyDispatcher::Result::Pass:
        return false;
    case KeyDispatcher::Result::Handled:
        return true;
    case KeyDispatcher::Result::NeedsSynthetic:
        // Re-dispatch as a synthetic event so the focused widget sees the
        // translated key (e.g. a QListWidget's default arrow-key navigation).
        if (QWidget *target = QApplication::focusWidget()) {
            m_dispatchingSyntheticKey = true;
            QKeyEvent translated(QEvent::KeyPress, r.syntheticKey, r.syntheticMods);
            QApplication::sendEvent(target, &translated);
            m_dispatchingSyntheticKey = false;
        }
        return true;
    }
    return false;
}
