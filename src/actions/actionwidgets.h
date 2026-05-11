#pragma once

#include "writetarget.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <utility>

constexpr int kActionDialogMinWidth = 800;

namespace ActionDialogInternal {

// Invokes `cb` whenever the watched object receives a resize event. Used by
// ActionDialogBuilder::setPreview to drive a re-render when the preview pane
// changes size (setPixmap alone doesn't trigger a label resize, so callbacks
// that render at the pane's resolution need to listen to Qt directly).
class ResizeWatcher : public QObject {
public:
    ResizeWatcher(std::function<void()> cb, QObject *parent)
        : QObject(parent), m_cb(std::move(cb)) {}
protected:
    bool eventFilter(QObject *, QEvent *e) override {
        if (e->type() == QEvent::Resize) m_cb();
        return false;
    }
private:
    std::function<void()> m_cb;
};

}  // namespace ActionDialogInternal

// Builds the parameter dialog for an action — header ("Inputs: N files"),
// caller-supplied rows, then an output-directory line edit + overwrite-policy
// combo + OK/Cancel button box. Decorates a caller-owned QDialog so callers
// can set the window title, install event filters, override flags, etc.
//
// Usage:
//
//   QDialog dlg(parent);
//   dlg.setWindowTitle("Resize");
//   ActionDialogBuilder b(&dlg, inputs);
//   b.addRow("Mode",  modeBox);
//   b.addRow("Value", valueSpin);
//   b.addOutputControls(defaultOutDir, m_overwrite);
//   const auto r = b.exec();
//   if (!r.accepted) return false;
//   m_outDir    = r.outDir;
//   m_overwrite = r.overwrite;
//
// `resizable=true` for dialogs whose preview benefits from extra real estate
// (Caption, Concatenate, Crop, Grid). Default is fixed-size.
//
// Calling setPreview(w) promotes the dialog to a two-column QSplitter layout:
// all form rows (including those added later via addRow / addOutputControls)
// sit in the left column inside a QScrollArea; `w` claims the right column.
// The button box is appended below the splitter. Only meaningful when
// resizable=true.
class ActionDialogBuilder {
public:
    ActionDialogBuilder(QDialog *dlg, const QStringList &inputs, bool resizable = false)
        : m_dialog(dlg) {
        m_dialog->setMinimumWidth(kActionDialogMinWidth);

        if (resizable) {
            m_dialog->setWindowFlags(m_dialog->windowFlags()
                                     | Qt::WindowMinimizeButtonHint
                                     | Qt::WindowMaximizeButtonHint);
            m_dialog->setSizeGripEnabled(true);
            // Preview-bearing dialogs benefit from real estate on first open;
            // the user can still restore down via the window controls.
            m_dialog->setWindowState(Qt::WindowMaximized);
        }

        m_root = new QVBoxLayout(m_dialog);
        if (!resizable)
            m_root->setSizeConstraint(QLayout::SetFixedSize);

        // Pin opening width via a min on the header label so SetFixedSize
        // honors the kActionDialogMinWidth on non-resizable dialogs.
        auto *header = new QLabel(QString("Inputs: %1 file%2")
                                      .arg(inputs.size())
                                      .arg(inputs.size() == 1 ? "" : "s"),
                                  m_dialog);
        QFont f = header->font();
        f.setBold(true);
        header->setFont(f);
        header->setMinimumWidth(kActionDialogMinWidth);
        m_root->addWidget(header);

        // Form layout is built unparented and inserted into the dialog at
        // exec() time — that's when we know whether setPreview() was called
        // and need to choose between single-column and two-column layouts.
        m_form = new QFormLayout;
    }

    void addRow(const QString &label, QWidget *field) { m_form->addRow(label, field); }
    void addRow(QLabel *label, QWidget *field)        { m_form->addRow(label, field); }
    void addRow(QWidget *spanning)                    { m_form->addRow(spanning); }

    // Adds the output-directory line edit (with a "..." picker), the overwrite-
    // policy combo, and the OK/Cancel button box. Call once, after your custom
    // rows. The output-dir field can be obtained via outDirEdit() if the action
    // needs to bind its value to other widgets (e.g., copy-or-move).
    void addOutputControls(const QString &defaultOutDir,
                           WriteTarget::Overwrite defaultOverwrite =
                               WriteTarget::Overwrite::Overwrite) {
        m_outDirEdit = new QLineEdit(defaultOutDir, m_dialog);

        auto *row = new QWidget(m_dialog);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *browse = new QPushButton("...", row);
        browse->setMaximumWidth(40);
        browse->setToolTip("Choose output directory");
        rowLayout->addWidget(m_outDirEdit);
        rowLayout->addWidget(browse);
        QObject::connect(browse, &QPushButton::clicked, m_dialog,
                         [edit = m_outDirEdit, dlg = m_dialog] {
            const QString picked = QFileDialog::getExistingDirectory(
                dlg, "Choose output directory", edit->text());
            if (!picked.isEmpty()) edit->setText(picked);
        });
        m_form->addRow("Output directory", row);

        m_overwriteBox = new QComboBox(m_dialog);
        m_overwriteBox->addItem("Overwrite existing files",
                                static_cast<int>(WriteTarget::Overwrite::Overwrite));
        m_overwriteBox->addItem("Skip existing files",
                                static_cast<int>(WriteTarget::Overwrite::Skip));
        m_overwriteBox->addItem("Rename (foo_1.jpg, foo_2.jpg, ...)",
                                static_cast<int>(WriteTarget::Overwrite::Rename));
        for (int i = 0; i < m_overwriteBox->count(); ++i) {
            if (m_overwriteBox->itemData(i).toInt() == static_cast<int>(defaultOverwrite)) {
                m_overwriteBox->setCurrentIndex(i);
                break;
            }
        }
        m_form->addRow("If output exists", m_overwriteBox);

        // Stash the button box for exec() to append below the form/splitter.
        m_buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, m_dialog);
        QObject::connect(m_buttons, &QDialogButtonBox::accepted, m_dialog, &QDialog::accept);
        QObject::connect(m_buttons, &QDialogButtonBox::rejected, m_dialog, &QDialog::reject);
    }

    // Returns nullptr before addOutputControls. Used by actions that need
    // to react to / mutate the output directory before exec() (copy-or-move
    // toggles its default between /copy and /move on mode change).
    QLineEdit *outDirEdit() const { return m_outDirEdit; }

    // Promotes the layout to two columns: form on the left in a scroll area,
    // `preview` on the right inside a draggable QSplitter. Stretch ratio
    // favors the preview but the user can drag the splitter handle. Optional
    // `onResize` runs whenever the preview pane changes size — wire it to
    // your re-render lambda for resolution-adaptive previews. Call before
    // exec(); only sensible on resizable dialogs.
    void setPreview(QWidget *preview, std::function<void()> onResize = {}) {
        m_previewWidget   = preview;
        m_previewOnResize = std::move(onResize);
    }

    struct Outcome {
        bool                   accepted = false;
        QString                outDir;
        WriteTarget::Overwrite overwrite = WriteTarget::Overwrite::Overwrite;
    };

    // Runs the dialog. accepted=false on user-cancel or empty output dir.
    Outcome exec() {
        // Assemble the body now that we know if setPreview() was called.
        // Single-column path drops the form straight under the header; two-
        // column path wraps the form in a QScrollArea and pairs it with the
        // preview inside a horizontal splitter.
        if (m_previewWidget) {
            // Pack the form rows at the top of the host widget; a trailing
            // stretch absorbs the extra vertical space the scroll area gives
            // us. Without the stretch, QFormLayout would distribute the slack
            // across its rows and label/field pairs would drift apart.
            auto *formHost = new QWidget(m_dialog);
            auto *vbox = new QVBoxLayout(formHost);
            vbox->setContentsMargins(0, 0, 0, 0);
            vbox->addLayout(m_form);
            vbox->addStretch(1);

            auto *scroll = new QScrollArea(m_dialog);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(formHost);
            // Wider default for the params column so the field rows have
            // breathing room — sizeHint alone leaves them cramped against
            // the long labels. User can drag the splitter handle to override.
            scroll->setMinimumWidth(550);

            auto *splitter = new QSplitter(Qt::Horizontal, m_dialog);
            splitter->addWidget(scroll);
            splitter->addWidget(m_previewWidget);
            // 2:3 ratio favors the preview but keeps the form noticeably
            // wider than its sizeHint when the dialog is maximized.
            splitter->setStretchFactor(0, 2);
            splitter->setStretchFactor(1, 3);
            splitter->setChildrenCollapsible(false);

            m_root->addWidget(splitter, /*stretch=*/1);

            if (m_previewOnResize)
                m_previewWidget->installEventFilter(
                    new ActionDialogInternal::ResizeWatcher(
                        m_previewOnResize, m_dialog));
        } else {
            m_root->addLayout(m_form);
        }
        if (m_buttons) m_root->addWidget(m_buttons);

        Outcome r;
        if (m_dialog->exec() != QDialog::Accepted) return r;
        if (!m_outDirEdit || !m_overwriteBox)      return r;
        const QString outDir = m_outDirEdit->text().trimmed();
        if (outDir.isEmpty()) return r;
        r.accepted  = true;
        r.outDir    = outDir;
        r.overwrite = static_cast<WriteTarget::Overwrite>(
            m_overwriteBox->currentData().toInt());
        return r;
    }

private:
    QDialog              *m_dialog;
    QVBoxLayout          *m_root          = nullptr;
    QFormLayout          *m_form          = nullptr;
    QWidget              *m_previewWidget = nullptr;
    std::function<void()> m_previewOnResize;
    QDialogButtonBox     *m_buttons       = nullptr;
    QLineEdit            *m_outDirEdit    = nullptr;
    QComboBox            *m_overwriteBox  = nullptr;
};
