#include "copymoveaction.h"

#include "actionlogger.h"
#include "actionwidgets.h"

#include <QButtonGroup>
#include <QFile>
#include <QFileInfo>
#include <QRadioButton>

bool CopyMoveAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Copy or move");

    auto shell = beginActionDialog(&dlg, inputs);

    auto *copyRadio = new QRadioButton("Copy", &dlg);
    auto *moveRadio = new QRadioButton("Move", &dlg);
    if (m_mode == Mode::Move) moveRadio->setChecked(true);
    else                      copyRadio->setChecked(true);

    auto *group = new QButtonGroup(&dlg);
    group->addButton(copyRadio, 0);
    group->addButton(moveRadio, 1);

    shell.form->addRow("Mode", copyRadio);
    shell.form->addRow("",     moveRadio);

    finishActionDialog(shell, &dlg, defaultOutDir, m_overwrite);

    // Default the output subdir based on the chosen mode (copy/ vs move/).
    // Auto-rename when the user toggles, but only if they haven't manually
    // edited the field. defaultOutDir comes in as `<srcDir>/copy-or-move`
    // (the action id), which we replace with `/copy` or `/move`.
    QString srcDir;
    if (!inputs.isEmpty()) srcDir = QFileInfo(inputs.first()).absolutePath();
    else                   srcDir = QFileInfo(defaultOutDir).path();
    const QString copyDir = srcDir + "/copy";
    const QString moveDir = srcDir + "/move";
    shell.outDirEdit->setText(m_mode == Mode::Move ? moveDir : copyDir);

    QObject::connect(copyRadio, &QRadioButton::toggled, &dlg,
        [outEdit = shell.outDirEdit, copyDir, moveDir](bool on) {
            if (on && outEdit->text() == moveDir) outEdit->setText(copyDir);
        });
    QObject::connect(moveRadio, &QRadioButton::toggled, &dlg,
        [outEdit = shell.outDirEdit, copyDir, moveDir](bool on) {
            if (on && outEdit->text() == copyDir) outEdit->setText(moveDir);
        });

    if (dlg.exec() != QDialog::Accepted) return false;
    const auto sh = readShellResults(shell);
    if (!sh) return false;
    m_mode      = (group->checkedId() == 1 ? Mode::Move : Mode::Copy);
    m_outDir    = sh->outDir;
    m_overwrite = sh->overwrite;
    return true;
}

QString CopyMoveAction::applyOne(const QString &input, ActionLogger *logger) {
    if (m_mode == Mode::Copy) {
        return writeOne(input, logger, [&](const QString &tempPath) {
            return QFile::copy(input, tempPath);
        });
    }

    // Move: try a direct rename to the resolved final path first — that's
    // atomic on the same filesystem. On cross-device failure, copy to a
    // temp via writeOne and then remove the source.
    const QString finalPath = resolveOutputPath(input, logger);
    if (finalPath.isEmpty()) return {};

    if (QFile::exists(finalPath)) QFile::remove(finalPath);
    if (QFile::rename(input, finalPath)) {
        if (logger) logger->info(QString("moved %1 -> %2").arg(input, finalPath));
        return finalPath;
    }

    const QString out = writeOne(input, logger, [&](const QString &tempPath) {
        return QFile::copy(input, tempPath);
    });
    if (out.isEmpty()) return {};
    QFile::remove(input);
    return out;
}
