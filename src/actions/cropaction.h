#pragma once

#include "action.h"

#include <QRect>

class CropAction : public BatchAction {
public:
    QString id() const override          { return "crop"; }
    QString name() const override        { return "Crop"; }
    QString description() const override { return "Crop one or more same-size images to the same rectangle"; }
    QKeySequence shortcut() const override { return QKeySequence("Alt+C"); }

    bool acceptsCount(int n) const override { return n >= 1; }

    bool configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) override;

protected:
    QString applyOne(const QString &input, ActionLogger *logger) override;

private:
    // Rect is in absolute pixel coordinates of the source image. Validity
    // (non-empty, inside image bounds) is enforced before configure() returns.
    QRect m_rect;
};
