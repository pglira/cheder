#pragma once

#include "action.h"

class CopyMoveAction : public BatchAction {
public:
    QString id() const override          { return "copy-or-move"; }
    QString name() const override        { return "Copy or move"; }
    QString description() const override { return "Copy or move files to a directory"; }

    bool configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) override;

protected:
    QString applyOne(const QString &input, ActionLogger *logger) override;

private:
    enum class Mode { Copy, Move };
    Mode m_mode = Mode::Copy;
};
