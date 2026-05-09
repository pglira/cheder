#include "resizeaction.h"

#include "actionwidgets.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

bool ResizeAction::configure(QWidget *parent, const QString &defaultOutDir) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Resize");

    auto *modeBox = new QComboBox(&dlg);
    modeBox->addItem("Longest edge (px)", static_cast<int>(Mode::LongestEdgePx));
    modeBox->addItem("Scale (%)",         static_cast<int>(Mode::ScalePercent));
    modeBox->setCurrentIndex(m_mode == Mode::LongestEdgePx ? 0 : 1);

    auto *valueSpin = new QSpinBox(&dlg);
    valueSpin->setRange(1, 100000);
    valueSpin->setValue(m_value);

    auto syncSpinRange = [valueSpin, modeBox]() {
        const auto mode = static_cast<Mode>(modeBox->currentData().toInt());
        if (mode == Mode::ScalePercent) {
            valueSpin->setRange(1, 1000);
            valueSpin->setSuffix(" %");
        } else {
            valueSpin->setRange(1, 100000);
            valueSpin->setSuffix(" px");
        }
    };
    syncSpinRange();
    QObject::connect(modeBox, &QComboBox::currentIndexChanged, &dlg, syncSpinRange);

    auto *outEdit = new QLineEdit(defaultOutDir, &dlg);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow("Mode",             modeBox);
    form->addRow("Value",            valueSpin);
    form->addRow("Output directory", outputDirField(outEdit, &dlg));

    auto *root = new QVBoxLayout(&dlg);
    root->addLayout(form);
    root->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    m_mode   = static_cast<Mode>(modeBox->currentData().toInt());
    m_value  = valueSpin->value();
    m_outDir = outEdit->text().trimmed();
    if (m_outDir.isEmpty()) return false;
    return true;
}

QString ResizeAction::applyOne(const QString &input) {
    QImageReader reader(input);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) return {};

    QSize target;
    if (m_mode == Mode::LongestEdgePx) {
        const int longest = std::max(img.width(), img.height());
        if (longest <= 0) return {};
        const double f = static_cast<double>(m_value) / longest;
        target = QSize(std::max(1, qRound(img.width()  * f)),
                       std::max(1, qRound(img.height() * f)));
    } else {
        const double f = m_value / 100.0;
        target = QSize(std::max(1, qRound(img.width()  * f)),
                       std::max(1, qRound(img.height() * f)));
    }

    QImage scaled = img.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QDir().mkpath(m_outDir);
    const QFileInfo fi(input);
    const QString outPath = m_outDir + '/' + fi.fileName();

    QImageWriter writer(outPath);
    if (!writer.write(scaled)) return {};
    return outPath;
}
