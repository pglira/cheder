#pragma once

#include "action.h"

class RenameAction : public BatchAction {
public:
    QString id() const override          { return "rename"; }
    QString name() const override        { return "Rename"; }
    QString description() const override { return "Rename a single image (and optionally move it)"; }
    QKeySequence shortcut() const override { return QKeySequence("F2"); }

    // Single-image only; the dialog has one "New filename" field, so a
    // multi-input rename would apply the same name to every input — never
    // what the user wants.
    bool acceptsCount(int n) const override { return n == 1; }

    bool configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) override;

protected:
    QString applyOne(const QString &input, ActionLogger *logger) override;

private:
    QString m_newName;
};
