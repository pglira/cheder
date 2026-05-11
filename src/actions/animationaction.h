#pragma once

#include "action.h"
#include "writetarget.h"

#include <QString>
#include <QStringList>

class AnimationAction : public Action {
public:
    QString id() const override            { return "animation"; }
    QString name() const override          { return "Animation"; }
    QString description() const override   { return "Build an animated GIF or MP4 from same-size frames"; }
    QKeySequence shortcut() const override { return QKeySequence("Alt+A"); }

    bool acceptsCount(int n) const override { return n >= 2; }

    bool configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir) override;
    QStringList apply(const QStringList &inputs, ActionLogger *logger) override;

    enum class Format  { Gif, Mp4 };
    enum class Quality { Low, Medium, High };  // CRF 28 / 23 / 18 for MP4

private:
    Format  m_format     = Format::Gif;
    int     m_fps        = 10;
    int     m_loops      = 0;  // GIF only: 0 = infinite
    Quality m_quality    = Quality::Medium;

    QString m_outDir;
    QString m_outFilename = "animation.gif";
    WriteTarget::Overwrite m_overwrite = WriteTarget::Overwrite::Overwrite;
};
