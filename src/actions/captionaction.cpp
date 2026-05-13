#include "captionaction.h"

#include "actionwidgets.h"
#include "imageio.h"

#include <QFont>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QFrame>
#include <QImage>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QSpinBox>

namespace {

// QLabel that holds a source QImage and renders it scaled-to-fit on every
// resize. Lets the action dialog grow/shrink without clipping the pixmap.
class PreviewLabel : public QLabel {
public:
    using QLabel::QLabel;
    void setSource(const QImage &img) { m_src = img; updateScaled(); }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QLabel::resizeEvent(e);
        updateScaled();
    }

private:
    void updateScaled() {
        if (m_src.isNull()) return;
        const QSize box = size();
        if (box.width() <= 0 || box.height() <= 0) return;
        QImage scaled = m_src.scaled(box, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        setPixmap(QPixmap::fromImage(scaled));
    }

    QImage m_src;
};

}  // namespace

bool CaptionAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir, ActionLogger *logger) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Caption");

    ActionDialogBuilder b(&dlg, inputs, /*resizable=*/true);

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

    b.addRow("Caption",    captionEdit);
    b.addRow("Position",   positionBox);
    b.addRow("Background", bgBox);
    b.addRow("Text color", fgBox);
    b.addRow("Font",       fontBox);
    b.addRow("Size",       sizeSpin);

    QImage srcOrig;
    if (!inputs.isEmpty()) srcOrig = readImage(inputs.first());

    auto *previewLabel = new PreviewLabel(&dlg);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setMinimumSize(320, 220);
    previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewLabel->setFrameShape(QFrame::StyledPanel);
    if (srcOrig.isNull())
        previewLabel->setText("(preview unavailable)");
    b.setPreview(previewLabel);

    // Render at the source's full resolution with the configured pointSize.
    // Display-time scaling is delegated to PreviewLabel, which fits the
    // result into whatever space the user has resized the label to —
    // identical renderCaptioned() inputs to applyOne(), no clipping.
    auto updatePreview = [=]() {
        if (srcOrig.isNull()) return;
        const QString caption = captionEdit->text();
        QImage out;
        if (caption.isEmpty()) {
            out = srcOrig;
        } else {
            const Position pos = static_cast<Position>(positionBox->currentData().toInt());
            const Bg bgv       = static_cast<Bg>(bgBox->currentData().toInt());
            const Fg fgv       = static_cast<Fg>(fgBox->currentData().toInt());
            const QString fam  = fontBox->currentFont().family();
            const int sz       = sizeSpin->value();
            out = renderCaptioned(srcOrig, caption, pos, bgv, fgv, fam, sz);
        }
        previewLabel->setSource(out);
    };

    QObject::connect(captionEdit, &QLineEdit::textChanged,         &dlg, updatePreview);
    QObject::connect(positionBox, &QComboBox::currentIndexChanged, &dlg, updatePreview);
    QObject::connect(bgBox,       &QComboBox::currentIndexChanged, &dlg, updatePreview);
    QObject::connect(fgBox,       &QComboBox::currentIndexChanged, &dlg, updatePreview);
    QObject::connect(fontBox,     &QFontComboBox::currentFontChanged, &dlg, updatePreview);
    QObject::connect(sizeSpin,    &QSpinBox::valueChanged,         &dlg, updatePreview);
    updatePreview();

    b.addOutputControls(defaultOutDir, m_overwrite);

    // Apply/Close mode: Apply commits the current widget state into the
    // action's members and runs apply() against the original inputs. Each
    // click is one full action invocation; the user can keep tweaking and
    // re-applying until Close.
    b.setApplyMode([this, inputs, logger,
                    captionEdit, positionBox, bgBox, fgBox, fontBox, sizeSpin]
                   (const ActionDialogBuilder::Outcome &o) {
        const QString caption = captionEdit->text();
        if (caption.isEmpty()) return;
        m_caption    = caption;
        m_position   = static_cast<Position>(positionBox->currentData().toInt());
        m_bg         = static_cast<Bg>(bgBox->currentData().toInt());
        m_fg         = static_cast<Fg>(fgBox->currentData().toInt());
        m_fontFamily = fontBox->currentFont().family();
        m_pointSize  = sizeSpin->value();
        m_outDir     = o.outDir;
        m_overwrite  = o.overwrite;
        apply(inputs, logger);
    }, logger);

    return b.exec().accepted;
}

QImage CaptionAction::renderCaptioned(const QImage &src,
                                      const QString &caption,
                                      Position position,
                                      Bg bg, Fg fg,
                                      const QString &fontFamily,
                                      int pointSize) {
    QFont font(fontFamily.isEmpty() ? QFont().family() : fontFamily);
    font.setPointSize(pointSize);

    const int margin = std::max(8, pointSize / 2);
    const QFontMetrics fm(font);
    const QRect textBound = fm.boundingRect(
        QRect(0, 0, std::max(1, src.width() - 2 * margin), 1'000'000),
        Qt::AlignHCenter | Qt::TextWordWrap, caption);
    const int stripHeight = textBound.height() + margin * 2;

    QColor bgColor;
    bool transparent = false;
    switch (bg) {
    case Bg::White:       bgColor = Qt::white;       break;
    case Bg::Black:       bgColor = Qt::black;       break;
    case Bg::Transparent: bgColor = Qt::transparent; transparent = true; break;
    }
    const QColor fgColor = (fg == Fg::White) ? Qt::white : Qt::black;

    const QImage::Format fmt = (transparent || src.hasAlphaChannel())
        ? QImage::Format_ARGB32_Premultiplied
        : QImage::Format_RGB32;

    QImage out(src.width(), src.height() + stripHeight, fmt);
    out.fill(transparent ? QColor(0, 0, 0, 0) : bgColor);

    const int imgY   = (position == Position::Top) ? stripHeight : 0;
    const int stripY = (position == Position::Top) ? 0 : src.height();

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.drawImage(0, imgY, src);
    p.setFont(font);
    p.setPen(fgColor);
    p.drawText(QRect(margin, stripY, src.width() - 2 * margin, stripHeight),
               Qt::AlignCenter | Qt::TextWordWrap, caption);
    p.end();
    return out;
}

QString CaptionAction::applyOne(const QString &input, ActionLogger *logger) {
    const QImage src = readImage(input);
    if (src.isNull()) return {};

    const QImage out = renderCaptioned(src, m_caption, m_position, m_bg, m_fg,
                                       m_fontFamily, m_pointSize);

    return writeOne(input, logger, [&](const QString &temp) {
        return QImageWriter(temp).write(out);
    });
}
