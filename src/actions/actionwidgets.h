#pragma once

#include "writetarget.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

constexpr int kActionDialogMinWidth = 800;

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
// (Caption, Concatenate). Default is fixed-size.
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
        }

        auto *root = new QVBoxLayout(m_dialog);
        if (!resizable)
            root->setSizeConstraint(QLayout::SetFixedSize);

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
        root->addWidget(header);

        m_form = new QFormLayout;
        root->addLayout(m_form);
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

        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, m_dialog);
        QObject::connect(buttons, &QDialogButtonBox::accepted, m_dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, m_dialog, &QDialog::reject);
        if (auto *vbox = qobject_cast<QVBoxLayout *>(m_dialog->layout()))
            vbox->addWidget(buttons);
    }

    // Returns nullptr before addOutputControls. Used by actions that need
    // to react to / mutate the output directory before exec() (copy-or-move
    // toggles its default between /copy and /move on mode change).
    QLineEdit *outDirEdit() const { return m_outDirEdit; }

    struct Outcome {
        bool                   accepted = false;
        QString                outDir;
        WriteTarget::Overwrite overwrite = WriteTarget::Overwrite::Overwrite;
    };

    // Runs the dialog. accepted=false on user-cancel or empty output dir.
    Outcome exec() {
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
    QDialog     *m_dialog;
    QFormLayout *m_form;
    QLineEdit   *m_outDirEdit   = nullptr;
    QComboBox   *m_overwriteBox = nullptr;
};
