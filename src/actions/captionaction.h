#pragma once

#include "action.h"

class CaptionAction : public BatchAction {
public:
    QString id() const override          { return "caption"; }
    QString name() const override        { return "Caption"; }
    QString description() const override { return "Add a text caption above or below the image"; }

    bool configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) override;

protected:
    QString applyOne(const QString &input, ActionLogger *logger) override;

private:
    enum class Position { Bottom, Top };
    enum class Bg       { White, Black, Transparent };
    enum class Fg       { Black, White };

    QString  m_caption;
    Position m_position  = Position::Bottom;
    Bg       m_bg        = Bg::White;
    Fg       m_fg        = Fg::Black;
    QString  m_fontFamily;
    int      m_pointSize = 32;
};
