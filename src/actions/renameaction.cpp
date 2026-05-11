#include "renameaction.h"

#include "actionwidgets.h"
#include "writetarget.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>

bool RenameAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) {
    if (inputs.size() != 1) return false;  // belt-and-braces; acceptsCount already enforces

    QDialog dlg(parent);
    dlg.setWindowTitle("Rename");
    ActionDialogBuilder b(&dlg, inputs);

    const QString original = QFileInfo(inputs.first()).fileName();
    auto *nameEdit = new QLineEdit(original, &dlg);
    b.addRow("New filename", nameEdit);
    b.addOutputControls(defaultOutDir, m_overwrite);

    // Mirror desktop file-manager F2 ergonomics: the stem is pre-selected so
    // typing replaces just "DSC_1234" leaving ".jpg" intact. Deferred via
    // singleShot so the focus/selection lands after the dialog is shown
    // (setFocus before show is best-effort at best).
    const int stemLen = QFileInfo(original).completeBaseName().size();
    QTimer::singleShot(0, &dlg, [nameEdit, stemLen] {
        nameEdit->setFocus();
        nameEdit->setSelection(0, stemLen);
    });

    const auto r = b.exec();
    if (!r.accepted) return false;

    const QString newName = nameEdit->text().trimmed();
    if (newName.isEmpty()) return false;
    if (newName.contains('/') || newName.contains('\\')) {
        QMessageBox::warning(parent, "Rename",
            "Filename cannot contain path separators ('/' or '\\'). "
            "Use the output-directory field to move to another folder.");
        return false;
    }
    // No-op when the resulting full path equals the source path. Comparing
    // full absolute paths handles both "same name, same dir" and a user
    // typing the output dir as a different but-equivalent path.
    const QString srcAbs    = QFileInfo(inputs.first()).absoluteFilePath();
    const QString targetAbs = QFileInfo(r.outDir + '/' + newName).absoluteFilePath();
    if (srcAbs == targetAbs) return false;

    m_newName   = newName;
    m_outDir    = r.outDir;
    m_overwrite = r.overwrite;
    return true;
}

QString RenameAction::applyOne(const QString &input, ActionLogger *logger) {
    // Output filename is user-specified (m_newName), not derived from input,
    // so bypass BatchAction::resolveOutputPath (which derives from input) and
    // resolve via WriteTarget with the new name. `avoidIfSame = input` so
    // Rename mode doesn't pick a candidate that resolves back to the source.
    const auto resolved = WriteTarget::resolve(m_outDir, m_newName, m_overwrite,
                                               logger, input);
    if (resolved.status != WriteTarget::ResolveStatus::Ok) return {};
    return WriteTarget::move(input, resolved.path, logger, "renamed");
}
