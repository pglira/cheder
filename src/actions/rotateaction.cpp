#include "rotateaction.h"

#include "actionwidgets.h"

#include <QButtonGroup>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QRadioButton>
#include <QTransform>

bool RotateAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Rotate");

    auto shell = beginActionDialog(&dlg, inputs);

    auto *cw   = new QRadioButton("90° clockwise", &dlg);
    auto *ccw  = new QRadioButton("90° counter-clockwise", &dlg);
    auto *flip = new QRadioButton("180°", &dlg);
    cw->setChecked(true);

    auto *group = new QButtonGroup(&dlg);
    group->addButton(cw,   90);
    group->addButton(ccw, -90);
    group->addButton(flip, 180);

    shell.form->addRow("Angle", cw);
    shell.form->addRow("",      ccw);
    shell.form->addRow("",      flip);

    finishActionDialog(shell, &dlg, defaultOutDir, m_overwrite);

    if (dlg.exec() != QDialog::Accepted) return false;

    m_angle     = group->checkedId();
    m_outDir    = shell.outDirEdit->text().trimmed();
    m_overwrite = overwriteFromBox(shell.overwriteBox);
    if (m_outDir.isEmpty()) return false;
    return true;
}

QString RotateAction::applyOne(const QString &input, ActionLogger *logger) {
    QImageReader reader(input);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) return {};

    QTransform t;
    t.rotate(m_angle);
    img = img.transformed(t, Qt::SmoothTransformation);

    return writeOne(input, logger, [&](const QString &tempPath) {
        QImageWriter writer(tempPath);
        return writer.write(img);
    });
}
