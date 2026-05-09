#include "rotateaction.h"

#include "actionwidgets.h"

#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QLineEdit>
#include <QRadioButton>
#include <QTransform>
#include <QVBoxLayout>

bool RotateAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Rotate");

    auto *cw   = new QRadioButton("90° clockwise", &dlg);
    auto *ccw  = new QRadioButton("90° counter-clockwise", &dlg);
    auto *flip = new QRadioButton("180°", &dlg);
    cw->setChecked(true);

    auto *group = new QButtonGroup(&dlg);
    group->addButton(cw,   90);
    group->addButton(ccw, -90);
    group->addButton(flip, 180);

    auto *outEdit = new QLineEdit(defaultOutDir, &dlg);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow("Angle", cw);
    form->addRow("",      ccw);
    form->addRow("",      flip);
    form->addRow("Output directory", outputDirField(outEdit, &dlg));

    auto *root = new QVBoxLayout(&dlg);
    root->addWidget(makeInputsLabel(inputs.size(), &dlg));
    root->addLayout(form);
    root->addWidget(buttons);
    styleActionDialog(dlg);

    if (dlg.exec() != QDialog::Accepted) return false;

    m_angle  = group->checkedId();
    m_outDir = outEdit->text().trimmed();
    if (m_outDir.isEmpty()) return false;
    return true;
}

QString RotateAction::applyOne(const QString &input) {
    QImageReader reader(input);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) return {};

    QTransform t;
    t.rotate(m_angle);
    img = img.transformed(t, Qt::SmoothTransformation);

    QDir().mkpath(m_outDir);
    const QFileInfo fi(input);
    const QString outPath = m_outDir + '/' + fi.fileName();

    QImageWriter writer(outPath);
    if (!writer.write(img)) return {};
    return outPath;
}
