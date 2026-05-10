#include "captionaction.h"

#include "actionwidgets.h"

#include <QFont>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QLineEdit>
#include <QPainter>
#include <QSpinBox>

bool CaptionAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Caption");

    auto shell = beginActionDialog(&dlg, inputs);

    auto *captionEdit = new QLineEdit(m_caption, &dlg);
    captionEdit->setPlaceholderText("Caption text (required)");

    auto *positionBox = new QComboBox(&dlg);
    positionBox->addItem("Bottom", static_cast<int>(Position::Bottom));
    positionBox->addItem("Top",    static_cast<int>(Position::Top));
    positionBox->setCurrentIndex(positionBox->findData(static_cast<int>(m_position)));

    auto *bgBox = new QComboBox(&dlg);
    bgBox->addItem("White",       static_cast<int>(Bg::White));
    bgBox->addItem("Black",       static_cast<int>(Bg::Black));
    bgBox->addItem("Transparent", static_cast<int>(Bg::Transparent));
    bgBox->setCurrentIndex(bgBox->findData(static_cast<int>(m_bg)));

    auto *fgBox = new QComboBox(&dlg);
    fgBox->addItem("Black", static_cast<int>(Fg::Black));
    fgBox->addItem("White", static_cast<int>(Fg::White));
    fgBox->setCurrentIndex(fgBox->findData(static_cast<int>(m_fg)));

    auto *fontBox = new QFontComboBox(&dlg);
    if (!m_fontFamily.isEmpty()) fontBox->setCurrentFont(QFont(m_fontFamily));

    auto *sizeSpin = new QSpinBox(&dlg);
    sizeSpin->setRange(6, 500);
    sizeSpin->setSuffix(" pt");
    sizeSpin->setValue(m_pointSize);

    shell.form->addRow("Caption",    captionEdit);
    shell.form->addRow("Position",   positionBox);
    shell.form->addRow("Background", bgBox);
    shell.form->addRow("Text color", fgBox);
    shell.form->addRow("Font",       fontBox);
    shell.form->addRow("Size",       sizeSpin);

    finishActionDialog(shell, &dlg, defaultOutDir, m_overwrite);

    if (dlg.exec() != QDialog::Accepted) return false;

    m_caption = captionEdit->text();
    if (m_caption.isEmpty()) return false;

    m_position   = static_cast<Position>(positionBox->currentData().toInt());
    m_bg         = static_cast<Bg>(bgBox->currentData().toInt());
    m_fg         = static_cast<Fg>(fgBox->currentData().toInt());
    m_fontFamily = fontBox->currentFont().family();
    m_pointSize  = sizeSpin->value();
    m_outDir     = shell.outDirEdit->text().trimmed();
    m_overwrite  = overwriteFromBox(shell.overwriteBox);
    if (m_outDir.isEmpty()) return false;
    return true;
}

QString CaptionAction::applyOne(const QString &input, ActionLogger *logger) {
    QImageReader reader(input);
    reader.setAutoTransform(true);
    QImage src = reader.read();
    if (src.isNull()) return {};

    QFont font(m_fontFamily.isEmpty() ? QFont().family() : m_fontFamily);
    font.setPointSize(m_pointSize);

    // Measure the wrapped caption against the source width so the strip
    // grows for long captions instead of clipping.
    const int margin = std::max(8, m_pointSize / 2);
    const QFontMetrics fm(font);
    const QRect textBound = fm.boundingRect(
        QRect(0, 0, std::max(1, src.width() - 2 * margin), 1'000'000),
        Qt::AlignHCenter | Qt::TextWordWrap, m_caption);
    const int stripHeight = textBound.height() + margin * 2;

    QColor bgColor;
    bool transparent = false;
    switch (m_bg) {
    case Bg::White:       bgColor = Qt::white;       break;
    case Bg::Black:       bgColor = Qt::black;       break;
    case Bg::Transparent: bgColor = Qt::transparent; transparent = true; break;
    }
    const QColor fgColor = (m_fg == Fg::White) ? Qt::white : Qt::black;

    const QImage::Format fmt = (transparent || src.hasAlphaChannel())
        ? QImage::Format_ARGB32_Premultiplied
        : QImage::Format_RGB32;

    QImage out(src.width(), src.height() + stripHeight, fmt);
    out.fill(transparent ? QColor(0, 0, 0, 0) : bgColor);

    const int imgY   = (m_position == Position::Top) ? stripHeight : 0;
    const int stripY = (m_position == Position::Top) ? 0 : src.height();

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.drawImage(0, imgY, src);
    p.setFont(font);
    p.setPen(fgColor);
    p.drawText(QRect(margin, stripY, src.width() - 2 * margin, stripHeight),
               Qt::AlignCenter | Qt::TextWordWrap, m_caption);
    p.end();

    return writeOne(input, logger, [&](const QString &temp) {
        return QImageWriter(temp).write(out);
    });
}
