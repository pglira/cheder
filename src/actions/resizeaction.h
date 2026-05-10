#pragma once

#include "action.h"

class ResizeAction : public BatchAction {
public:
    QString id() const override          { return "resize"; }
    QString name() const override        { return "Resize"; }
    QString description() const override { return "Scale by longest edge or percent"; }

    bool configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) override;

protected:
    QString applyOne(const QString &input, ActionLogger *logger) override;

private:
    enum class Mode { LongestEdgePx, ScalePercent };
    Mode m_mode    = Mode::LongestEdgePx;
    int  m_pixels  = 1024;  // for LongestEdgePx
    int  m_percent = 50;    // for ScalePercent
};
