#include "copymoveaction.h"

#include "actionlogger.h"
#include "actionwidgets.h"
#include "writetarget.h"

#include <QButtonGroup>
#include <QFile>
#include <QFileInfo>
#include <QRadioButton>

bool CopyMoveAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Copy or move");

    ActionDialogBuilder b(&dlg, inputs);

    auto *copyRadio = new QRadioButton("Copy", &dlg);
    auto *moveRadio = new QRadioButton("Move", &dlg);
    if (m_mode == Mode::Move) moveRadio->setChecked(true);
    else                      copyRadio->setChecked(true);

    auto *group = new QButtonGroup(&dlg);
    group->addButton(copyRadio, 0);
    group->addButton(moveRadio, 1);

    b.addRow("Mode", copyRadio);
    b.addRow("",     moveRadio);
    b.addOutputControls(defaultOutDir, m_overwrite);

    // Default the output subdir based on the chosen mode (copy/ vs move/).
    // Auto-rename when the user toggles, but only if they haven't manually
    // edited the field. defaultOutDir comes in as `<srcDir>/copy-or-move`
    // (the action id), which we replace with `/copy` or `/move`.
    QString srcDir;
    if (!inputs.isEmpty()) srcDir = QFileInfo(inputs.first()).absolutePath();
    else                   srcDir = QFileInfo(defaultOutDir).path();
    const QString copyDir = srcDir + "/copy";
    const QString moveDir = srcDir + "/move";
    b.outDirEdit()->setText(m_mode == Mode::Move ? moveDir : copyDir);

    QObject::connect(copyRadio, &QRadioButton::toggled, &dlg,
        [outEdit = b.outDirEdit(), copyDir, moveDir](bool on) {
            if (on && outEdit->text() == moveDir) outEdit->setText(copyDir);
        });
    QObject::connect(moveRadio, &QRadioButton::toggled, &dlg,
        [outEdit = b.outDirEdit(), copyDir, moveDir](bool on) {
            if (on && outEdit->text() == copyDir) outEdit->setText(moveDir);
        });

    const auto r = b.exec();
    if (!r.accepted) return false;
    m_mode      = (group->checkedId() == 1 ? Mode::Move : Mode::Copy);
    m_outDir    = r.outDir;
    m_overwrite = r.overwrite;
    return true;
}

QString CopyMoveAction::applyOne(const QString &input, ActionLogger *logger) {
    if (m_mode == Mode::Copy) {
        return writeOne(input, logger, [&](const QString &tempPath) {
            return QFile::copy(input, tempPath);
        });
    }

    // Move: resolve under our overwrite policy, then hand off to the
    // shared atomic-move helper (rename, with cross-fs copy+delete fallback).
    const QString finalPath = resolveOutputPath(input, logger);
    if (finalPath.isEmpty()) return {};
    return WriteTarget::move(input, finalPath, logger, "moved");
}
