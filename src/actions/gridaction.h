#pragma once

#include "action.h"
#include "writetarget.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>

class GridAction : public Action {
public:
    QString id() const override            { return "grid"; }
    QString name() const override          { return "Grid"; }
    QString description() const override   { return "Arrange images into a grid with optional titles"; }
    QKeySequence shortcut() const override { return QKeySequence("Alt+G"); }

    bool acceptsCount(int n) const override { return n >= 2; }
    bool supportsMultiApply() const override { return true; }

    bool configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir, ActionLogger *logger) override;
    QStringList apply(const QStringList &inputs, ActionLogger *logger) override;

    enum class TitleSource { None, Filename, Custom };

private:
    // Single renderer used by both the configure() preview and the apply()
    // write so the dialog mirrors the saved output exactly. titles must have
    // the same length as srcs; for TitleSource::None it is ignored.
    static QImage renderGrid(const QList<QImage> &srcs,
                             const QStringList &titles,
                             int cols,
                             int cellW, int cellH,
                             int hSpacing, int vSpacing,
                             const QColor &bgColor,
                             const QColor &titleColor,
                             const QString &fontFamily,
                             int fontPointSize,
                             TitleSource titleSource);

    int     m_cols          = 0;     // 0 = derive ceil(sqrt(N)) on first open
    int     m_cellW         = 600;
    int     m_cellH         = 600;
    int     m_hSpacing      = 0;
    int     m_vSpacing      = 0;
    QColor  m_bgColor       = Qt::white;
    QColor  m_titleColor    = Qt::black;
    QString m_fontFamily;            // empty = system default
    int     m_fontPointSize = 14;
    TitleSource m_titleSource = TitleSource::Filename;

    QStringList m_orderedInputs;          // user-reordered in the dialog; apply() uses this
    QHash<QString, QString> m_titles;     // keyed by full path so opening Grid on a
                                          // different input set doesn't bleed stale
                                          // titles through by index

    QString m_outDir;
    QString m_outFilename = "grid.png";
    WriteTarget::Overwrite m_overwrite = WriteTarget::Overwrite::Overwrite;
};
