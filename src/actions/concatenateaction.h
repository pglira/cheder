#pragma once

#include "action.h"
#include "writetarget.h"

#include <QImage>
#include <QStringList>

class ConcatenateAction : public Action {
public:
    QString id() const override          { return "concatenate"; }
    QString name() const override        { return "Concatenate"; }
    QString description() const override { return "Concatenate images horizontally or vertically"; }
    QKeySequence shortcut() const override { return QKeySequence("Alt+N"); }

    bool acceptsCount(int n) const override { return n >= 2; }

    bool configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) override;
    QStringList apply(const QStringList &inputs, ActionLogger *logger) override;

    // Public so the configure() preview and the apply() write can share one
    // renderer (and so that file-scope helpers in the .cpp can name them).
    enum class Orientation { Horizontal, Vertical };
    enum class Bg          { White, Black, Transparent };

private:
    static QImage renderConcat(const QList<QImage> &srcs,
                               Orientation orient,
                               int targetAxis,
                               int spacing,
                               Bg bg);

    Orientation m_orientation = Orientation::Horizontal;
    int         m_targetAxis  = 1000;
    int         m_spacing     = 0;
    Bg          m_bg          = Bg::Transparent;
    QStringList m_orderedInputs;  // user-reordered in the dialog; apply() uses this
    QString     m_outDir;
    QString     m_outFilename = "concat.png";
    WriteTarget::Overwrite m_overwrite = WriteTarget::Overwrite::Overwrite;
};
