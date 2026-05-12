#include "rotateaction.h"

#include "actionwidgets.h"
#include "imageio.h"

#include <QButtonGroup>
#include <QImage>
#include <QImageWriter>
#include <QRadioButton>
#include <QTransform>

bool RotateAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir, ActionLogger *logger) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Rotate");

    ActionDialogBuilder b(&dlg, inputs);

    auto *cw   = new QRadioButton("90° clockwise", &dlg);
    auto *ccw  = new QRadioButton("90° counter-clockwise", &dlg);
    auto *flip = new QRadioButton("180°", &dlg);
    cw->setChecked(true);

    auto *group = new QButtonGroup(&dlg);
    group->addButton(cw,   90);
    group->addButton(ccw, -90);
    group->addButton(flip, 180);

    b.addRow("Angle", cw);
    b.addRow("",      ccw);
    b.addRow("",      flip);
    b.addOutputControls(defaultOutDir, m_overwrite);

    const auto r = b.exec();
    if (!r.accepted) return false;
    m_angle     = group->checkedId();
    m_outDir    = r.outDir;
    m_overwrite = r.overwrite;
    return true;
}

QString RotateAction::applyOne(const QString &input, ActionLogger *logger) {
    QImage img = readImage(input);
    if (img.isNull()) return {};

    QTransform t;
    t.rotate(m_angle);
    img = img.transformed(t, Qt::SmoothTransformation);

    return writeOne(input, logger, [&](const QString &tempPath) {
        QImageWriter writer(tempPath);
        return writer.write(img);
    });
}
