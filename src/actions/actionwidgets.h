#pragma once

#include "action.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

// Set as a minimum so the dialog opens wide but the user can grow it.
constexpr int kActionDialogMinWidth = 800;

inline QLabel *makeInputsLabel(int count, QWidget *parent) {
    auto *label = new QLabel(parent);
    label->setText(QString("Inputs: %1 file%2").arg(count).arg(count == 1 ? "" : "s"));
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

inline void styleActionDialog(QDialog &dlg) {
    dlg.setMinimumWidth(kActionDialogMinWidth);
}

// Wraps `edit` with a "..." button that opens a directory picker; the picker
// prefills with the edit's current text and writes the choice back. Returns
// the composed row, suitable as a QFormLayout field widget.
inline QWidget *outputDirField(QLineEdit *edit, QWidget *parent) {
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *browse = new QPushButton("...", row);
    browse->setMaximumWidth(40);
    browse->setToolTip("Choose output directory");
    layout->addWidget(edit);
    layout->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, parent, [edit, parent] {
        const QString picked = QFileDialog::getExistingDirectory(
            parent, "Choose output directory", edit->text());
        if (!picked.isEmpty()) edit->setText(picked);
    });
    return row;
}

// Shared scaffolding so each action's configure() only describes its own
// parameter rows. The pattern is:
//
//   QDialog dlg(parent);
//   dlg.setWindowTitle("Resize");
//   auto shell = beginActionDialog(&dlg, inputs);
//   shell.form->addRow("Mode",  modeBox);
//   shell.form->addRow("Value", valueSpin);
//   finishActionDialog(shell, &dlg, defaultOutDir, m_overwrite);
//   if (dlg.exec() != QDialog::Accepted) return false;
//   m_outDir    = shell.outDirEdit->text().trimmed();
//   m_overwrite = overwriteFromBox(shell.overwriteBox);
struct ActionDialogShell {
    QFormLayout *form         = nullptr;  // action adds its rows here
    QLineEdit   *outDirEdit   = nullptr;  // populated by finishActionDialog
    QComboBox   *overwriteBox = nullptr;  // populated by finishActionDialog
};

// Plain parameter dialogs (Resize, Rotate, CopyMove) have no preview and
// nothing that benefits from extra space, so default to a fixed-size frame.
// Pass `resizable = true` for dialogs that *do* benefit from growing —
// e.g. CaptionAction's preview pane.
inline ActionDialogShell beginActionDialog(QDialog *dlg, const QStringList &inputs,
                                           bool resizable = false) {
    styleActionDialog(*dlg);

    if (resizable) {
        dlg->setWindowFlags(dlg->windowFlags()
                            | Qt::WindowMinimizeButtonHint
                            | Qt::WindowMaximizeButtonHint);
        dlg->setSizeGripEnabled(true);
    }

    auto *root = new QVBoxLayout(dlg);
    if (!resizable)
        root->setSizeConstraint(QLayout::SetFixedSize);

    // Pin the opening width via a min on the header label rather than the
    // dialog itself — that way it flows through the layout's sizeHint and
    // SetFixedSize honors it for non-resizable dialogs.
    auto *header = makeInputsLabel(inputs.size(), dlg);
    header->setMinimumWidth(kActionDialogMinWidth);
    root->addWidget(header);

    ActionDialogShell shell;
    shell.form = new QFormLayout;
    root->addLayout(shell.form);
    return shell;
}

inline void finishActionDialog(ActionDialogShell &shell, QDialog *dlg,
                               const QString &defaultOutDir,
                               BatchAction::Overwrite defaultOverwrite =
                                   BatchAction::Overwrite::Overwrite) {
    shell.outDirEdit = new QLineEdit(defaultOutDir, dlg);
    shell.form->addRow("Output directory", outputDirField(shell.outDirEdit, dlg));

    shell.overwriteBox = new QComboBox(dlg);
    shell.overwriteBox->addItem("Overwrite existing files",
                                static_cast<int>(BatchAction::Overwrite::Overwrite));
    shell.overwriteBox->addItem("Skip existing files",
                                static_cast<int>(BatchAction::Overwrite::Skip));
    shell.overwriteBox->addItem("Rename (foo_1.jpg, foo_2.jpg, ...)",
                                static_cast<int>(BatchAction::Overwrite::Rename));
    for (int i = 0; i < shell.overwriteBox->count(); ++i) {
        if (shell.overwriteBox->itemData(i).toInt() == static_cast<int>(defaultOverwrite)) {
            shell.overwriteBox->setCurrentIndex(i);
            break;
        }
    }
    shell.form->addRow("If output exists", shell.overwriteBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    if (auto *vbox = qobject_cast<QVBoxLayout *>(dlg->layout()))
        vbox->addWidget(buttons);
}

inline BatchAction::Overwrite overwriteFromBox(QComboBox *box) {
    return static_cast<BatchAction::Overwrite>(box->currentData().toInt());
}

// Copies the dialog's output dir + overwrite policy back onto `action`.
// Returns false if the user accepted with an empty output directory (treated
// the same as cancel by callers). Every action does these three lines after
// dlg.exec() succeeds, so factor them out.
inline bool applyShellResults(const ActionDialogShell &shell, BatchAction &action) {
    const QString outDir = shell.outDirEdit->text().trimmed();
    if (outDir.isEmpty()) return false;
    action.setOutDir(outDir);
    action.setOverwrite(overwriteFromBox(shell.overwriteBox));
    return true;
}
