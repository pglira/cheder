#include "copymoveaction.h"

#include "actionwidgets.h"

#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QRadioButton>
#include <QVBoxLayout>

bool CopyMoveAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Copy or move");

    auto *copyRadio = new QRadioButton("Copy", &dlg);
    auto *moveRadio = new QRadioButton("Move", &dlg);
    if (m_mode == Mode::Move) moveRadio->setChecked(true);
    else                      copyRadio->setChecked(true);

    auto *group = new QButtonGroup(&dlg);
    group->addButton(copyRadio, 0);
    group->addButton(moveRadio, 1);

    // Default the output subdir based on the chosen mode (copy/ vs move/).
    // Auto-rename when the user toggles, but only if they haven't manually
    // edited the field.
    QString srcDir;
    if (!inputs.isEmpty()) srcDir = QFileInfo(inputs.first()).absolutePath();
    else                   srcDir = QFileInfo(defaultOutDir).path();
    const QString copyDir = srcDir + "/copy";
    const QString moveDir = srcDir + "/move";
    const QString initial = (m_mode == Mode::Move ? moveDir : copyDir);

    auto *outEdit = new QLineEdit(initial, &dlg);
    QObject::connect(copyRadio, &QRadioButton::toggled, &dlg, [outEdit, copyDir, moveDir](bool on) {
        if (on && outEdit->text() == moveDir) outEdit->setText(copyDir);
    });
    QObject::connect(moveRadio, &QRadioButton::toggled, &dlg, [outEdit, copyDir, moveDir](bool on) {
        if (on && outEdit->text() == copyDir) outEdit->setText(moveDir);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow("Mode",             copyRadio);
    form->addRow("",                 moveRadio);
    form->addRow("Output directory", outputDirField(outEdit, &dlg));

    auto *root = new QVBoxLayout(&dlg);
    root->addWidget(makeInputsLabel(inputs.size(), &dlg));
    root->addLayout(form);
    root->addWidget(buttons);
    styleActionDialog(dlg);

    if (dlg.exec() != QDialog::Accepted) return false;

    m_mode   = (group->checkedId() == 1 ? Mode::Move : Mode::Copy);
    m_outDir = outEdit->text().trimmed();
    if (m_outDir.isEmpty()) return false;
    return true;
}

QString CopyMoveAction::applyOne(const QString &input) {
    QDir().mkpath(m_outDir);
    const QFileInfo fi(input);
    const QString outPath = m_outDir + '/' + fi.fileName();

    // Refuse to copy/move onto self (e.g. user pointed output dir at source dir).
    if (QFileInfo(outPath).absoluteFilePath() == QFileInfo(input).absoluteFilePath())
        return {};

    if (QFile::exists(outPath)) QFile::remove(outPath);

    if (m_mode == Mode::Copy)
        return QFile::copy(input, outPath) ? outPath : QString{};

    // Move: try rename first (instant on same filesystem). If that fails
    // (cross-device, etc.), fall back to copy + remove.
    if (QFile::rename(input, outPath)) return outPath;
    if (!QFile::copy(input, outPath)) return {};
    QFile::remove(input);
    return outPath;
}
