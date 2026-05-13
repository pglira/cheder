#pragma once

#include "action.h"
#include "writetarget.h"

#include <QColor>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>

// Tagged union of the four annotation shapes. Stored in source-image
// coordinates so the saved-output rendering at native resolution matches
// the dialog preview proportionally. Each shape carries its own style so
// changing the dialog controls after placing a shape only affects future
// shapes (and, once selection lands, the currently-selected one).
enum class AnnotateShapeType { Rect, Circle, Arrow, Text };

struct AnnotateShape {
    AnnotateShapeType type;
    QRect    rect;          // Rect, Circle: normalized bounding box.
    QPoint   p1, p2;        // Arrow: tail, head. Text uses p1 as the top-left anchor.
    QString  text;          // Text.
    QColor   color;         // All shapes.
    double   strokeWidth = 0.0;  // Rect, Circle, Arrow: source-pixel stroke.
    int      fontPx      = 0;    // Text: source-pixel font size.
    QString  fontFamily;         // Text: font family ("" = system default).

    // Required so the undo stack can skip no-op snapshots and so QList's
    // copy-elision tricks behave. Compares every field — there's no notion
    // of "irrelevant for this type"; storing the unused fields uniformly is
    // cheap.
    bool operator==(const AnnotateShape &o) const {
        return type == o.type && rect == o.rect && p1 == o.p1 && p2 == o.p2
            && text == o.text && color == o.color
            && strokeWidth == o.strokeWidth && fontPx == o.fontPx
            && fontFamily == o.fontFamily;
    }
    bool operator!=(const AnnotateShape &o) const { return !(*this == o); }
};

class AnnotateAction : public Action {
public:
    QString id() const override            { return "annotate"; }
    QString name() const override          { return "Annotate"; }
    QString description() const override   { return "Draw shapes and text on a single image"; }
    QKeySequence shortcut() const override { return QKeySequence("Alt+D"); }

    bool acceptsCount(int n) const override { return n == 1; }
    bool supportsMultiApply() const override { return true; }

    bool configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir, ActionLogger *logger) override;
    QStringList apply(const QStringList &inputs, ActionLogger *logger) override;

private:
    // Source-coordinate shapes captured from the canvas on OK. Cleared at
    // the start of each configure() call so the dialog always opens blank.
    QList<AnnotateShape> m_shapes;

    QString m_outDir;
    // Output-filename template with `{stem}` / `{ext}` placeholders; rendered
    // per apply() via WriteTarget::renderFilename. Default is `_annotated.png`
    // (literal `.png`) so the alpha channel for annotation shapes survives
    // even when the input is a JPEG.
    QString m_outFilenameTemplate;
    WriteTarget::Overwrite m_overwrite = WriteTarget::Overwrite::Overwrite;
};
