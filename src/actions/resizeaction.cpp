#include "resizeaction.h"

#include "actionwidgets.h"

#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QSpinBox>

bool ResizeAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Resize");

    auto shell = beginActionDialog(&dlg, inputs);

    auto *modeBox = new QComboBox(&dlg);
    modeBox->addItem("Longest edge (px)", static_cast<int>(Mode::LongestEdgePx));
    modeBox->addItem("Scale (%)",         static_cast<int>(Mode::ScalePercent));
    modeBox->setCurrentIndex(m_mode == Mode::LongestEdgePx ? 0 : 1);

    auto *valueSpin = new QSpinBox(&dlg);
    valueSpin->setRange(1, 100000);

    // Each mode has its own remembered value, so toggling Mode swaps the
    // spinner to a sensible default (1024 px or 50 %) instead of clamping.
    auto syncSpinRange = [valueSpin, modeBox, this]() {
        const auto mode = static_cast<Mode>(modeBox->currentData().toInt());
        if (mode == Mode::ScalePercent) {
            valueSpin->setRange(1, 1000);
            valueSpin->setSuffix(" %");
            valueSpin->setValue(m_percent);
        } else {
            valueSpin->setRange(1, 100000);
            valueSpin->setSuffix(" px");
            valueSpin->setValue(m_pixels);
        }
    };
    syncSpinRange();
    QObject::connect(modeBox, &QComboBox::currentIndexChanged, &dlg, syncSpinRange);

    shell.form->addRow("Mode",  modeBox);
    shell.form->addRow("Value", valueSpin);

    finishActionDialog(shell, &dlg, defaultOutDir, m_overwrite);

    if (dlg.exec() != QDialog::Accepted) return false;

    m_mode = static_cast<Mode>(modeBox->currentData().toInt());
    if (m_mode == Mode::ScalePercent) m_percent = valueSpin->value();
    else                              m_pixels  = valueSpin->value();
    m_outDir    = shell.outDirEdit->text().trimmed();
    m_overwrite = overwriteFromBox(shell.overwriteBox);
    if (m_outDir.isEmpty()) return false;
    return true;
}

QString ResizeAction::applyOne(const QString &input, ActionLogger *logger) {
    QImageReader reader(input);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) return {};

    QSize target;
    if (m_mode == Mode::LongestEdgePx) {
        const int longest = std::max(img.width(), img.height());
        if (longest <= 0) return {};
        const double f = static_cast<double>(m_pixels) / longest;
        target = QSize(std::max(1, qRound(img.width()  * f)),
                       std::max(1, qRound(img.height() * f)));
    } else {
        const double f = m_percent / 100.0;
        target = QSize(std::max(1, qRound(img.width()  * f)),
                       std::max(1, qRound(img.height() * f)));
    }

    QImage scaled = img.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return writeOne(input, logger, [&](const QString &tempPath) {
        QImageWriter writer(tempPath);
        return writer.write(scaled);
    });
}
