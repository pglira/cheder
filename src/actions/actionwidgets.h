#pragma once

#include "actionlogger.h"
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
        // Empty when the dialog didn't add a filename field. Otherwise the
        // user's template — `{stem}` and `{ext}` placeholders rendered per
        // input at apply() time via WriteTarget::renderFilename.
        QString                outFilename;
    };

    // Adds a single "Output file" row containing a QLineEdit pre-filled with
    // `defaultTemplate` and an inline error label that surfaces underneath
    // when the user submits an empty template. The template uses `{stem}` /
    // `{ext}` placeholders (see WriteTarget::renderFilename). Tooltip on the
    // field explains the syntax; the row is appended to the form layout, so
    // call before addOutputControls() to keep the output group together.
    void addOutputFilenameField(const QString &defaultTemplate) {
        m_outFilenameEdit = new QLineEdit(defaultTemplate, m_dialog);
        m_outFilenameEdit->setToolTip(
            QStringLiteral("Use {stem} for the input's basename and "
                           "{ext} for its extension (no dot)."));

        m_outFilenameErrorLabel = new QLabel(
            QStringLiteral("Output filename is required."), m_dialog);
        m_outFilenameErrorLabel->setStyleSheet("color: #cc0000;");
        m_outFilenameErrorLabel->setVisible(false);

        auto *wrapper = new QWidget(m_dialog);
        auto *vbox = new QVBoxLayout(wrapper);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(2);
        vbox->addWidget(m_outFilenameEdit);
        vbox->addWidget(m_outFilenameErrorLabel);

        // Clear the error state the moment the user starts fixing it — no
        // need to wait for the next submit attempt to confirm the field is
        // populated again.
        QObject::connect(m_outFilenameEdit, &QLineEdit::textChanged, m_dialog,
            [this](const QString &) {
                if (m_outFilenameErrorLabel)
                    m_outFilenameErrorLabel->setVisible(false);
            });

        m_form->addRow(QStringLiteral("Output file"), wrapper);
    }

    // Switches the dialog's button strip from OK/Cancel to Apply/Close.
    // Apply invokes `onApply` with the current Outcome and leaves the
    // dialog open, so the user can tweak parameters and Apply again. Close
    // dismisses without firing another apply. exec()'s return value's
    // `accepted` is true if at least one Apply happened during the session.
    // Used by actions that opt in via supportsMultiApply().
    //
    // Passing `logger` adds a one-line status strip above the buttons that
    // mirrors the action's log output — without it, users running a maximized
    // dialog see no feedback because the MainWindow log dock is hidden behind.
    void setApplyMode(std::function<void(const Outcome &)> onApply,
                      ActionLogger *logger = nullptr) {
        m_applyCallback = std::move(onApply);
        m_logger        = logger;
    }

    // Snapshot of the current dialog state as an Outcome. Returns
    // accepted=false when the output dir is empty or (when an output-filename
    // field was added) when its template is empty — matches the OK/Cancel
    // exec()'s validation so the Apply handler can quietly skip invalid
    // submissions and so OK/Cancel can refuse to accept.
    Outcome currentOutcome() const {
        Outcome r;
        if (!m_outDirEdit || !m_overwriteBox) return r;
        const QString outDir = m_outDirEdit->text().trimmed();
        if (outDir.isEmpty()) return r;
        QString outFilename;
        if (m_outFilenameEdit) {
            outFilename = m_outFilenameEdit->text().trimmed();
            if (outFilename.isEmpty()) return r;
        }
        r.accepted    = true;
        r.outDir      = outDir;
        r.overwrite   = static_cast<WriteTarget::Overwrite>(
            m_overwriteBox->currentData().toInt());
        r.outFilename = outFilename;
        return r;
    }

    // Runs the dialog. In OK/Cancel mode, accepted=false on user-cancel or
    // empty output dir. In Apply/Close mode, accepted=true if at least one
    // Apply was clicked successfully during the session.
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

        // Button strip — defer here so we know whether Apply-mode was
        // requested by the caller (via setApplyMode). OK/Cancel stays the
        // default; Apply/Close keeps the dialog open and tracks whether
        // anything was committed.
        auto applied = std::make_shared<bool>(false);
        auto *buttons = new QDialogButtonBox(m_dialog);
        QLabel *statusLabel = nullptr;
        if (m_applyCallback) {
            auto *applyBtn = buttons->addButton(QStringLiteral("Apply"),
                                                QDialogButtonBox::ApplyRole);
            auto *closeBtn = buttons->addButton(QStringLiteral("Close"),
                                                QDialogButtonBox::RejectRole);
            QObject::connect(applyBtn, &QPushButton::clicked, m_dialog,
                [this, applied] {
                    if (!validateAndShowErrors()) return;
                    const Outcome o = currentOutcome();
                    if (!o.accepted) return;  // empty out dir — silently skip
                    m_applyCallback(o);
                    *applied = true;
                });
            QObject::connect(closeBtn, &QPushButton::clicked, m_dialog,
                             &QDialog::reject);

            // Status label mirrors the ActionLogger so a maximized dialog
            // (which fully covers MainWindow's log dock) still surfaces
            // "[Action] done — wrote N, skipped M, failed K". m_dialog as
            // connection context auto-disconnects when the dialog closes.
            // Placed inline with the button box (see HBox below).
            if (m_logger) {
                statusLabel = new QLabel(m_dialog);
                statusLabel->setTextFormat(Qt::PlainText);
                statusLabel->setMinimumHeight(statusLabel->fontMetrics().lineSpacing());
                QObject::connect(m_logger, &ActionLogger::logged, m_dialog,
                    [statusLabel](int level, const QString &message) {
                        const char *color =
                            level == ActionLogger::Error ? "#cc0000" :
                            level == ActionLogger::Warn  ? "#cc7700" : "";
                        statusLabel->setStyleSheet(
                            *color ? QString("color: %1;").arg(color)
                                   : QString());
                        statusLabel->setText(message);
                        statusLabel->setToolTip(message);
                    });
            }
        } else {
            buttons->addButton(QDialogButtonBox::Ok);
            buttons->addButton(QDialogButtonBox::Cancel);
            // Validate before accepting so the inline error label can surface
            // an empty filename without closing the dialog. accepted() fires
            // whether the user pressed Enter or clicked OK, so this catches
            // both paths.
            QObject::connect(buttons, &QDialogButtonBox::accepted, m_dialog,
                [this] {
                    if (!validateAndShowErrors()) return;
                    m_dialog->accept();
                });
            QObject::connect(buttons, &QDialogButtonBox::rejected, m_dialog, &QDialog::reject);
        }
        if (statusLabel) {
            auto *bottomRow = new QHBoxLayout;
            bottomRow->setContentsMargins(0, 0, 0, 0);
            bottomRow->addWidget(statusLabel, /*stretch=*/1);
            bottomRow->addWidget(buttons);
            m_root->addLayout(bottomRow);
        } else {
            m_root->addWidget(buttons);
        }

        const int code = m_dialog->exec();
        if (m_applyCallback) {
            // Multi-apply: accepted iff the user applied at least once.
            // outDir/overwrite from the current widget state — caller
            // already saw each Apply's state via the callback.
            if (!*applied) return Outcome{};
            return currentOutcome();
        }
        if (code != QDialog::Accepted) return Outcome{};
        return currentOutcome();
    }

private:
    // True if the dialog has all the required fields populated. Surfaces the
    // inline error label next to the filename field on failure; the
    // textChanged hook clears it as soon as the user starts typing.
    bool validateAndShowErrors() {
        if (!m_outFilenameEdit) return true;
        const bool ok = !m_outFilenameEdit->text().trimmed().isEmpty();
        if (m_outFilenameErrorLabel) m_outFilenameErrorLabel->setVisible(!ok);
        return ok;
    }

    QDialog              *m_dialog;
    QVBoxLayout          *m_root                   = nullptr;
    QFormLayout          *m_form                   = nullptr;
    QWidget              *m_previewWidget          = nullptr;
    std::function<void()> m_previewOnResize;
    QLineEdit            *m_outDirEdit             = nullptr;
    QComboBox            *m_overwriteBox           = nullptr;
    QLineEdit            *m_outFilenameEdit        = nullptr;
    QLabel               *m_outFilenameErrorLabel  = nullptr;
    std::function<void(const Outcome &)> m_applyCallback;
    ActionLogger         *m_logger                 = nullptr;
};
