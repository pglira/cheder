#pragma once

#include "action.h"

#include <QImage>

class CaptionAction : public BatchAction {
public:
    QString id() const override          { return "caption"; }
    QString name() const override        { return "Caption"; }
    QString description() const override { return "Add a text caption above or below a single image"; }
    QKeySequence shortcut() const override { return QKeySequence("Alt+T"); }

    // Captioning only makes sense one image at a time — the dialog presets a
    // single caption, font, and size; running it across a multi-selection
    // would slap the same text on every file.
    bool acceptsCount(int n) const override { return n == 1; }

    bool configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) override;

protected:
    QString applyOne(const QString &input, ActionLogger *logger) override;

private:
    enum class Position { Bottom, Top };
    enum class Bg       { White, Black, Transparent };
    enum class Fg       { Black, White };

    // Pure render: the configure() preview and applyOne() invoke this with
    // identical arguments (the dialog only scales the resulting image at
    // display time), so the preview is byte-faithful to what gets written.
    static QImage renderCaptioned(const QImage &src,
                                  const QString &caption,
                                  Position position,
                                  Bg bg, Fg fg,
                                  const QString &fontFamily,
                                  int pointSize);

    QString  m_caption;
    Position m_position  = Position::Bottom;
    Bg       m_bg        = Bg::White;
    Fg       m_fg        = Fg::Black;
    QString  m_fontFamily;
    int      m_pointSize = 32;
};
