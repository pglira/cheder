#pragma once

#include "action.h"

class RotateAction : public BatchAction {
public:
    QString id() const override          { return "rotate"; }
    QString name() const override        { return "Rotate"; }
    QString description() const override { return "Rotate by a fixed angle"; }

    bool configure(QWidget *parent, const QString &defaultOutDir) override;

protected:
    QString applyOne(const QString &input) override;

private:
    int m_angle = 90;  // 90, -90 (CCW), or 180
};
