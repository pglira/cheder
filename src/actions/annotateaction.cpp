#include "annotateaction.h"

#include "actionlogger.h"
#include "actionwidgets.h"
#include "imageio.h"
#include "writetarget.h"

#include <QButtonGroup>
#include <QColor>
#include <QColorDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageWriter>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>

namespace {

enum class Tool { None, Rect, Circle, Arrow, Text, Select };

// Programmatic tool-button icons. Drawing them at runtime keeps the action
// self-contained (no resource files, no theme-specific bitmaps) and lets the
// glyphs follow the dialog's ButtonText palette color so they read on both
// light and dark themes.
QPixmap makeToolIcon(Tool tool, const QColor &color, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing,     true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QPen pen(color, 2);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (tool) {
    case Tool::Rect:
        p.drawRect(QRectF(4, 6, size - 8, size - 12));
        break;
    case Tool::Circle:
        p.drawEllipse(QRectF(3, 3, size - 6, size - 6));
        break;
    case Tool::Arrow: {
        const QPointF tail(4, size - 4);
        const QPointF head(size - 4, 4);
        const double len = QLineF(tail, head).length();
        const QPointF dir = (head - tail) / len;
        const QPointF perp(-dir.y(), dir.x());
        const double headLen = std::min(8.0, len * 0.45);
        const double headW   = headLen * 0.625;
        const QPointF base = head - dir * headLen;
        p.drawLine(tail, base);
        QPolygonF tri;
        tri << head << (base + perp * headW) << (base - perp * headW);
        p.setBrush(color);
        p.drawPolygon(tri);
        break;
    }
    case Tool::Text: {
        QFont f;
        f.setPixelSize(size - 6);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, "A");
        break;
    }
    case Tool::Select: {
        // Classic NW-pointing cursor arrow.
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        QPolygonF cur;
        cur << QPointF(5, 3)
            << QPointF(5, size - 6)
            << QPointF(9, size - 10)
            << QPointF(12, size - 4)
            << QPointF(15, size - 5)
            << QPointF(12, size - 11)
            << QPointF(size - 7, size - 12);
        p.drawPolygon(cur);
        break;
    }
    case Tool::None:
        break;
    }
    return pm;
}

// Active Select-tool interaction. MoveShape drags the whole shape; the box
// handles map to which corner/edge of the bounding box follows the cursor;
// the arrow handles each pin one endpoint to the cursor.
enum class DragMode {
    None,
    MoveShape,
    HandleNW, HandleN, HandleNE, HandleE,
    HandleSE, HandleS, HandleSW, HandleW,
    ArrowTail, ArrowHead,
};

constexpr int kHandlePx = 10;       // display-space side length of each handle
constexpr int kSelectionOutset = 6; // display-px gap between bbox and the dashed selection rect

// Pixel-tolerance (source-space) for clicking thin shapes. Lines/borders
// shorter than this are still grabbable. Scales with stroke since thick
// shapes have more visible target area already.
double hitTolerance(double strokeWidth) {
    return std::max(4.0, strokeWidth);
}

// Source-coordinate bounding box of a shape. Used both for hit-testing
// non-line shapes and for drawing the selection overlay.
QRect shapeSourceBBox(const AnnotateShape &s) {
    const int half = std::max(1, qRound(s.strokeWidth / 2.0));
    switch (s.type) {
    case AnnotateShapeType::Rect:
    case AnnotateShapeType::Circle:
        return s.rect.normalized().adjusted(-half, -half, half, half);
    case AnnotateShapeType::Arrow: {
        QRect r(s.p1, s.p2);
        return r.normalized().adjusted(-half, -half, half, half);
    }
    case AnnotateShapeType::Text: {
        QFont f(s.fontFamily.isEmpty() ? QFont().family() : s.fontFamily);
        f.setPixelSize(std::max(1, s.fontPx));
        QFontMetrics fm(f);
        return QRect(s.p1, QSize(fm.horizontalAdvance(s.text), fm.height()));
    }
    }
    return {};
}

// Distance from point `q` to the line segment (a, b). Used for arrow hit
// testing — bbox alone is too generous on diagonal arrows.
double segmentDistance(QPointF q, QPointF a, QPointF b) {
    const QPointF ab = b - a;
    const QPointF aq = q - a;
    const double ab2 = QPointF::dotProduct(ab, ab);
    if (ab2 < 1e-6) return QLineF(a, q).length();
    const double t = std::clamp(QPointF::dotProduct(aq, ab) / ab2, 0.0, 1.0);
    const QPointF proj = a + ab * t;
    return QLineF(proj, q).length();
}

bool hitShape(const AnnotateShape &s, QPoint p) {
    const double tol = hitTolerance(s.strokeWidth);
    switch (s.type) {
    case AnnotateShapeType::Rect:
    case AnnotateShapeType::Circle:
    case AnnotateShapeType::Text:
        return shapeSourceBBox(s).adjusted(-qRound(tol), -qRound(tol),
                                           qRound(tol),  qRound(tol)).contains(p);
    case AnnotateShapeType::Arrow:
        return segmentDistance(QPointF(p), QPointF(s.p1), QPointF(s.p2)) <= tol;
    }
    return false;
}

// Display-space rectangle the dashed selection outline + handles live on,
// outset slightly from the shape's bounding box so a thick stroke stays
// clear of the dashed line.
QRectF selectionBoxDisp(const AnnotateShape &s, double scale, QPoint translate) {
    const QRect src = shapeSourceBBox(s);
    return QRectF(translate.x() + src.x() * scale - kSelectionOutset,
                  translate.y() + src.y() * scale - kSelectionOutset,
                  src.width()  * scale + kSelectionOutset * 2,
                  src.height() * scale + kSelectionOutset * 2);
}

// Display-space center points for the 8 box handles (NW..W), in the same
// order as DragMode::Handle* runs around the rect.
std::array<QPointF, 8> boxHandleCenters(QRectF box) {
    const double cx = box.center().x();
    const double cy = box.center().y();
    return {
        box.topLeft(),
        QPointF(cx, box.top()),
        box.topRight(),
        QPointF(box.right(), cy),
        box.bottomRight(),
        QPointF(cx, box.bottom()),
        box.bottomLeft(),
        QPointF(box.left(), cy),
    };
}

QRectF handleRectAt(QPointF center) {
    return QRectF(center.x() - kHandlePx / 2.0,
                  center.y() - kHandlePx / 2.0,
                  kHandlePx, kHandlePx);
}

// Returns the handle the mouse is over (in display coords) for the given
// selected shape — or DragMode::None if no handle is hit. Rect/Circle have
// 8 box handles; Arrow has 2 endpoint handles; Text has none (font size
// controls its dimensions).
DragMode hitHandle(QPointF mouseDisp, const AnnotateShape &s,
                   double scale, QPoint translate) {
    if (s.type == AnnotateShapeType::Arrow) {
        const QPointF tail(translate.x() + s.p1.x() * scale,
                           translate.y() + s.p1.y() * scale);
        const QPointF head(translate.x() + s.p2.x() * scale,
                           translate.y() + s.p2.y() * scale);
        if (handleRectAt(tail).contains(mouseDisp)) return DragMode::ArrowTail;
        if (handleRectAt(head).contains(mouseDisp)) return DragMode::ArrowHead;
        return DragMode::None;
    }
    if (s.type == AnnotateShapeType::Text) return DragMode::None;

    const QRectF box = selectionBoxDisp(s, scale, translate);
    const auto centers = boxHandleCenters(box);
    static constexpr DragMode modes[8] = {
        DragMode::HandleNW, DragMode::HandleN, DragMode::HandleNE, DragMode::HandleE,
        DragMode::HandleSE, DragMode::HandleS, DragMode::HandleSW, DragMode::HandleW,
    };
    for (int i = 0; i < 8; ++i)
        if (handleRectAt(centers[i]).contains(mouseDisp)) return modes[i];
    return DragMode::None;
}

// Resize the bounding rect by moving the corner/edge identified by `mode`
// to mouseSrc; the opposite corner/edge stays anchored to `original`. When
// `constrainRatio` is true (Shift held) and `mode` is one of the four
// corners, the dragged corner is projected onto the anchor->original-corner
// diagonal so the new rect keeps the original aspect ratio.
void applyBoxResize(QRect &rect, DragMode mode, QPoint mouseSrc, QRect original,
                    bool constrainRatio) {
    const bool isCorner = (mode == DragMode::HandleNW || mode == DragMode::HandleNE
                        || mode == DragMode::HandleSE || mode == DragMode::HandleSW);
    if (constrainRatio && isCorner) {
        QPoint anchor, origCorner;
        switch (mode) {
        case DragMode::HandleNW: anchor = original.bottomRight(); origCorner = original.topLeft();     break;
        case DragMode::HandleNE: anchor = original.bottomLeft();  origCorner = original.topRight();    break;
        case DragMode::HandleSE: anchor = original.topLeft();     origCorner = original.bottomRight(); break;
        case DragMode::HandleSW: anchor = original.topRight();    origCorner = original.bottomLeft();  break;
        default: break;
        }
        const QPointF dir(origCorner - anchor);
        const QPointF rel(mouseSrc - anchor);
        const double dirLen2 = QPointF::dotProduct(dir, dir);
        if (dirLen2 < 1e-6) return;
        // Scalar projection of mouse offset onto the diagonal. Negative t
        // (mouse dragged across the anchor) still flips the rect cleanly
        // after normalize().
        const double t = QPointF::dotProduct(rel, dir) / dirLen2;
        const QPointF newCorner = QPointF(anchor) + dir * t;
        rect = QRect(anchor,
                     QPoint(qRound(newCorner.x()), qRound(newCorner.y()))).normalized();
        return;
    }

    QPoint tl = original.topLeft();
    QPoint br = original.bottomRight();
    switch (mode) {
    case DragMode::HandleNW: tl = mouseSrc; break;
    case DragMode::HandleN:  tl.setY(mouseSrc.y()); break;
    case DragMode::HandleNE: tl.setY(mouseSrc.y()); br.setX(mouseSrc.x()); break;
    case DragMode::HandleE:  br.setX(mouseSrc.x()); break;
    case DragMode::HandleSE: br = mouseSrc; break;
    case DragMode::HandleS:  br.setY(mouseSrc.y()); break;
    case DragMode::HandleSW: tl.setX(mouseSrc.x()); br.setY(mouseSrc.y()); break;
    case DragMode::HandleW:  tl.setX(mouseSrc.x()); break;
    default: return;
    }
    rect = QRect(tl, br).normalized();
}

void translateShape(AnnotateShape &s, QPoint delta) {
    switch (s.type) {
    case AnnotateShapeType::Rect:
    case AnnotateShapeType::Circle:
        s.rect.translate(delta);
        break;
    case AnnotateShapeType::Arrow:
        s.p1 += delta;
        s.p2 += delta;
        break;
    case AnnotateShapeType::Text:
        s.p1 += delta;
        break;
    }
}

// Hardcoded defaults at first dialog open. Color & font also drive the
// initial state of the dialog controls; the spinners' ranges further below
// constrain user edits.
const QColor  kDefaultColor(220, 30, 30);
constexpr double kDefaultStroke = 4.0;

int defaultFontPx(const QImage &img) {
    if (img.isNull()) return 16;
    return std::max(16, std::max(img.width(), img.height()) / 50);
}

// Paint a single shape at the given paint scale. Used by both the canvas
// (scale = display/source) and the apply-time composite (scale = 1.0). The
// translate offset positions the result inside the widget's frame; the
// composite path passes (0,0).
void paintShape(QPainter &p, const AnnotateShape &s, double scale, QPoint translate) {
    auto toDisp = [&](QPoint sp) {
        return QPointF(translate.x() + sp.x() * scale,
                       translate.y() + sp.y() * scale);
    };
    auto toDispRect = [&](QRect sr) {
        return QRectF(translate.x() + sr.x() * scale,
                      translate.y() + sr.y() * scale,
                      sr.width()  * scale,
                      sr.height() * scale);
    };

    const double strokeDisp = std::max(1.0, s.strokeWidth * scale);
    QPen pen(s.color, strokeDisp);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (s.type) {
    case AnnotateShapeType::Rect:
        p.drawRect(toDispRect(s.rect));
        break;
    case AnnotateShapeType::Circle:
        p.drawEllipse(toDispRect(s.rect));
        break;
    case AnnotateShapeType::Arrow: {
        const QPointF tail = toDisp(s.p1);
        const QPointF head = toDisp(s.p2);

        const double dx = head.x() - tail.x();
        const double dy = head.y() - tail.y();
        const double len = std::hypot(dx, dy);
        if (len < 1e-3) break;

        // Arrowhead scales with stroke but never overruns the segment so
        // tiny arrows still look proportionate (ratio 4:2.5 preserved).
        const double headLen   = std::min(strokeDisp * 4.0, len * 0.7);
        const double headWidth = headLen * 0.625;
        const QPointF dir(dx / len, dy / len);
        const QPointF perp(-dir.y(), dir.x());
        const QPointF base = head - dir * headLen;

        // Line ends at the triangle's base, not its apex — otherwise the
        // pen's stroke cap pokes a small spike out past the arrowhead tip.
        p.drawLine(tail, base);

        QPolygonF tri;
        tri << head
            << (base + perp * headWidth)
            << (base - perp * headWidth);
        p.setBrush(s.color);
        p.drawPolygon(tri);
        p.setBrush(Qt::NoBrush);
        break;
    }
    case AnnotateShapeType::Text: {
        QFont f(s.fontFamily.isEmpty() ? QFont().family() : s.fontFamily);
        const int sizePx = std::max(1, qRound(s.fontPx * scale));
        f.setPixelSize(sizePx);
        p.setFont(f);
        // Top-left anchored at p1 — feels intuitive when clicking a spot
        // to drop text there. QPainter::drawText(QRect, flags, text) uses
        // an oversized rect so wrapping never kicks in.
        const QPointF anchor = toDisp(s.p1);
        p.setPen(s.color);
        p.drawText(QRectF(anchor, QSizeF(1e6, 1e6)),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextDontClip,
                   s.text);
        break;
    }
    }
}

class AnnotateCanvas : public QWidget {
public:
    struct Style {
        QColor  color       = kDefaultColor;
        double  strokeWidth = kDefaultStroke;
        int     fontPx      = 16;
        QString fontFamily;  // "" = system default
    };

    explicit AnnotateCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(400, 300);
        setMouseTracking(true);
        // StrongFocus so the canvas grabs focus on click and receives Delete
        // (which is otherwise consumed by the toolbox / spinboxes).
        setFocusPolicy(Qt::StrongFocus);
    }

    void setImage(const QImage &img) {
        m_image = img;
        m_shapes.clear();
        m_pending = {};
        m_dragging = false;
        m_dragMode = DragMode::None;
        m_pendingSnapshot.reset();
        m_undoStack.clear();
        m_selectedIndex = -1;
        m_style.fontPx = defaultFontPx(img);
        notifySelectionChanged();
        update();
    }

    void setTool(Tool t) {
        m_tool = t;
        // Base cursor by tool; mouseMoveEvent refines further when the
        // pointer enters a shape body or handle.
        setCursor(baseCursorFor(t));
    }

    // Style setters update both the "next shape" defaults and the currently
    // selected shape so the dialog controls feel live-WYSIWYG when a shape
    // is selected. With no selection they only touch m_style.
    void setColor(const QColor &c) {
        m_style.color = c;
        if (auto *s = mutableSelectedShape()) { s->color = c; update(); }
    }
    void setStrokeWidth(double w) {
        m_style.strokeWidth = w;
        if (auto *s = mutableSelectedShape()) { s->strokeWidth = w; update(); }
    }
    void setFontPx(int px) {
        m_style.fontPx = px;
        if (auto *s = mutableSelectedShape()) { s->fontPx = px; update(); }
    }
    void setFontFamily(const QString &fam) {
        m_style.fontFamily = fam;
        if (auto *s = mutableSelectedShape()) { s->fontFamily = fam; update(); }
    }
    const Style &style() const { return m_style; }

    // Fired whenever m_selectedIndex changes (including to -1 on
    // deselection). The dialog wires this to repopulate the style controls
    // from the new selection — or from m_style when nothing is selected.
    void setSelectionChangedCallback(std::function<void()> cb) {
        m_onSelectionChanged = std::move(cb);
    }
    const AnnotateShape *selectedShape() const {
        return (m_selectedIndex >= 0 && m_selectedIndex < m_shapes.size())
            ? &m_shapes[m_selectedIndex] : nullptr;
    }

    // Full-snapshot undo. Each entry stores the entire shape list as it
    // looked before a mutation; restoring is just assignment. Memory cost
    // scales with shape count, but annotation sessions stay tiny in
    // practice, so the simplicity is worth it.
    QList<AnnotateShape> snapshotShapes() const { return m_shapes; }

    void pushUndo(QList<AnnotateShape> preState) {
        // Skip entries that wouldn't change anything on restore — keeps the
        // stack clean when a "drag" produced no actual movement, or a style
        // edit didn't change the value.
        if (preState != m_shapes)
            m_undoStack.append(std::move(preState));
    }

    bool undo() {
        if (m_undoStack.isEmpty()) return false;
        // Abort any in-progress drag so the next mouseMove doesn't paint
        // over the restored state from a now-stale m_dragStartShape.
        m_dragMode = DragMode::None;
        m_dragging = false;
        m_pendingSnapshot.reset();

        m_shapes = m_undoStack.takeLast();
        if (m_selectedIndex >= m_shapes.size()) m_selectedIndex = -1;
        // Always notify — style of selected shape may have changed even
        // when m_selectedIndex didn't.
        notifySelectionChanged();
        update();
        return true;
    }

    const QImage              &image()  const { return m_image; }
    const QList<AnnotateShape> &shapes() const { return m_shapes; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(QRect(QPoint(0, 0), size()), QColor(0x22, 0x22, 0x22));
        if (m_image.isNull()) return;
        const QRect disp = imageDisplayRect();
        p.drawImage(disp, m_image);

        const double s = scale();
        for (const auto &shape : m_shapes) paintShape(p, shape, s, disp.topLeft());
        if (m_dragging) paintShape(p, m_pending, s, disp.topLeft());

        // Selection overlay drawn last so it sits above all shapes regardless
        // of stacking order. Dashed bbox outline + resize handles per shape
        // type (none for text — font size controls its dimensions).
        if (m_selectedIndex >= 0 && m_selectedIndex < m_shapes.size()) {
            const AnnotateShape &shape = m_shapes[m_selectedIndex];
            const QRectF boxDisp = selectionBoxDisp(shape, s, disp.topLeft());

            QPen selPen(QColor(50, 160, 255), 1.5, Qt::DashLine);
            p.setPen(selPen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(boxDisp);

            // Handles: filled white squares with a blue border for visibility
            // on both light and dark image content.
            p.setPen(QPen(QColor(50, 160, 255), 1.5));
            p.setBrush(QColor(255, 255, 255));
            if (shape.type == AnnotateShapeType::Arrow) {
                const QPointF tail(disp.x() + shape.p1.x() * s,
                                   disp.y() + shape.p1.y() * s);
                const QPointF head(disp.x() + shape.p2.x() * s,
                                   disp.y() + shape.p2.y() * s);
                p.drawEllipse(handleRectAt(tail));
                p.drawEllipse(handleRectAt(head));
            } else if (shape.type != AnnotateShapeType::Text) {
                for (const QPointF &c : boxHandleCenters(boxDisp))
                    p.drawRect(handleRectAt(c));
            }
        }
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton || m_image.isNull()) return;
        if (m_tool == Tool::None) return;  // all buttons toggled off — no interaction
        if (!imageDisplayRect().contains(e->pos())) return;

        const QPoint src = dispToSource(e->pos());

        if (m_tool == Tool::Select) {
            const double sc = scale();
            const QPoint trans = imageDisplayRect().topLeft();

            // Check handles of the currently-selected shape first so the user
            // can grab a handle even when it sits over a different shape.
            if (m_selectedIndex >= 0 && m_selectedIndex < m_shapes.size()) {
                const DragMode handle = hitHandle(
                    QPointF(e->pos()), m_shapes[m_selectedIndex], sc, trans);
                if (handle != DragMode::None) {
                    m_dragMode          = handle;
                    m_dragStartShape    = m_shapes[m_selectedIndex];
                    m_dragStartMouseSrc = src;
                    m_pendingSnapshot   = m_shapes;
                    return;
                }
            }

            // Otherwise hit-test shape bodies in topmost-first order.
            int idx = -1;
            for (int i = m_shapes.size() - 1; i >= 0; --i) {
                if (hitShape(m_shapes[i], src)) { idx = i; break; }
            }
            if (idx != m_selectedIndex) {
                m_selectedIndex = idx;
                notifySelectionChanged();
            }
            if (idx >= 0) {
                // Begin a move drag from the hit shape. Mouse-release with
                // zero net movement still leaves the shape selected.
                m_dragMode          = DragMode::MoveShape;
                m_dragStartShape    = m_shapes[idx];
                m_dragStartMouseSrc = src;
                m_pendingSnapshot   = m_shapes;
            }
            update();
            return;
        }

        if (m_tool == Tool::Text) {
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, "Annotate text", "Text:",
                QLineEdit::Normal, QString(), &ok);
            if (!ok || text.isEmpty()) return;
            auto pre = m_shapes;
            AnnotateShape s = freshShape();
            s.type   = AnnotateShapeType::Text;
            s.p1     = src;
            s.text   = text;
            m_shapes.append(s);
            pushUndo(std::move(pre));
            update();
            return;
        }

        m_dragStartSrc = src;
        m_pending = freshShape();
        m_pending.type = toShapeType(m_tool);
        if (m_pending.type == AnnotateShapeType::Arrow) {
            m_pending.p1 = src;
            m_pending.p2 = src;
        } else {
            m_pending.rect = QRect(src, src);
        }
        m_dragging = true;
        m_pendingSnapshot = m_shapes;
        update();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        // While not in a drag, refine the cursor based on what's under it.
        // setMouseTracking(true) ensures this fires even without a button.
        if (m_dragMode == DragMode::None && !m_dragging)
            setCursor(cursorForHover(e->pos()));

        if (m_dragMode != DragMode::None) {
            if (m_selectedIndex < 0 || m_selectedIndex >= m_shapes.size()) {
                m_dragMode = DragMode::None;
                return;
            }
            const QPoint cur = dispToSource(e->pos());
            AnnotateShape &s = m_shapes[m_selectedIndex];
            // Always derive the new geometry from m_dragStartShape so multi-
            // pixel cursor moves apply once, not cumulatively per move event.
            if (m_dragMode == DragMode::MoveShape) {
                s = m_dragStartShape;
                translateShape(s, cur - m_dragStartMouseSrc);
            } else if (m_dragMode == DragMode::ArrowTail) {
                s = m_dragStartShape;
                s.p1 = cur;
            } else if (m_dragMode == DragMode::ArrowHead) {
                s = m_dragStartShape;
                s.p2 = cur;
            } else {
                s = m_dragStartShape;
                applyBoxResize(s.rect, m_dragMode, cur, m_dragStartShape.rect,
                               e->modifiers() & Qt::ShiftModifier);
            }
            update();
            return;
        }
        if (!m_dragging) return;
        const QPoint cur = dispToSource(e->pos());
        if (m_pending.type == AnnotateShapeType::Arrow) {
            m_pending.p2 = cur;
        } else {
            QRect r(m_dragStartSrc, cur);
            // Shift held during a Circle drag constrains the bounding box
            // to a square so the result is a perfect circle. The longer
            // drag axis wins so the cursor stays close to the edge.
            if (m_pending.type == AnnotateShapeType::Circle
                    && (e->modifiers() & Qt::ShiftModifier)) {
                const int dx = cur.x() - m_dragStartSrc.x();
                const int dy = cur.y() - m_dragStartSrc.y();
                const int side = std::max(std::abs(dx), std::abs(dy));
                const int sx = (dx < 0) ? -side : side;
                const int sy = (dy < 0) ? -side : side;
                r = QRect(m_dragStartSrc,
                          QPoint(m_dragStartSrc.x() + sx, m_dragStartSrc.y() + sy));
            }
            m_pending.rect = r.normalized();
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) return;
        if (m_dragMode != DragMode::None) {
            // Normalize rect-based shapes after a resize so negative widths
            // (dragging past the opposite edge) don't break later hit tests.
            if (m_selectedIndex >= 0 && m_selectedIndex < m_shapes.size()) {
                auto &s = m_shapes[m_selectedIndex];
                if (s.type == AnnotateShapeType::Rect
                        || s.type == AnnotateShapeType::Circle) {
                    s.rect = s.rect.normalized();
                }
            }
            m_dragMode = DragMode::None;
            commitPendingSnapshot();
            update();
            return;
        }
        if (!m_dragging) return;
        m_dragging = false;
        if (isPendingMeaningful()) m_shapes.append(m_pending);
        m_pending = {};
        commitPendingSnapshot();
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override {
        // Double-click is text-edit only — and only under the Select tool.
        // In drawing tools the second press would otherwise create a second
        // shape under the same cursor, which is jarring.
        if (e->button() != Qt::LeftButton || m_image.isNull()) return;
        if (m_tool != Tool::Select) return;
        if (!imageDisplayRect().contains(e->pos())) return;

        const QPoint src = dispToSource(e->pos());
        // Find the topmost text shape under the cursor — skipping non-text
        // shapes means a text label partially obscured by a rectangle is
        // still editable without rearranging the stack.
        int idx = -1;
        for (int i = m_shapes.size() - 1; i >= 0; --i) {
            if (m_shapes[i].type == AnnotateShapeType::Text && hitShape(m_shapes[i], src)) {
                idx = i;
                break;
            }
        }
        if (idx < 0) return;

        bool ok = false;
        const QString updated = QInputDialog::getText(
            this, "Edit text", "Text:",
            QLineEdit::Normal, m_shapes[idx].text, &ok);
        if (!ok || updated.isEmpty()) return;

        auto pre = m_shapes;
        m_shapes[idx].text = updated;
        pushUndo(std::move(pre));
        if (idx != m_selectedIndex) {
            m_selectedIndex = idx;
            notifySelectionChanged();
        }
        update();
    }

    void keyPressEvent(QKeyEvent *e) override {
        if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
                && m_selectedIndex >= 0 && m_selectedIndex < m_shapes.size()) {
            auto pre = m_shapes;
            m_shapes.removeAt(m_selectedIndex);
            pushUndo(std::move(pre));
            m_selectedIndex = -1;
            notifySelectionChanged();
            update();
            return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    // Builds a shape pre-populated with the canvas's current style settings.
    // Each shape carries its own style so future-style edits don't mutate
    // already-placed shapes.
    AnnotateShape freshShape() const {
        AnnotateShape s;
        s.color       = m_style.color;
        s.strokeWidth = m_style.strokeWidth;
        s.fontPx      = m_style.fontPx;
        s.fontFamily  = m_style.fontFamily;
        return s;
    }

    AnnotateShape *mutableSelectedShape() {
        return (m_selectedIndex >= 0 && m_selectedIndex < m_shapes.size())
            ? &m_shapes[m_selectedIndex] : nullptr;
    }

    // End of a mouse-driven destructive op: push the snapshot onto the undo
    // stack (no-op if nothing actually changed, courtesy of pushUndo's
    // equality guard) and clear the in-progress flag.
    void commitPendingSnapshot() {
        if (m_pendingSnapshot) {
            pushUndo(std::move(*m_pendingSnapshot));
            m_pendingSnapshot.reset();
        }
    }

    void notifySelectionChanged() {
        if (m_onSelectionChanged) m_onSelectionChanged();
    }

    static Qt::CursorShape baseCursorFor(Tool t) {
        switch (t) {
        case Tool::None:
        case Tool::Select: return Qt::ArrowCursor;
        case Tool::Rect:
        case Tool::Circle:
        case Tool::Arrow:
        case Tool::Text:   return Qt::CrossCursor;
        }
        return Qt::ArrowCursor;
    }

    // Cursor shape for the current hover position. Honors per-handle resize
    // cursors when Select is active and the pointer is over a handle on the
    // selected shape; falls back to the tool's base cursor otherwise.
    Qt::CursorShape cursorForHover(QPoint mousePos) const {
        if (m_image.isNull()) return Qt::ArrowCursor;
        const bool insideImage = imageDisplayRect().contains(mousePos);

        if (m_tool == Tool::Select) {
            if (m_selectedIndex >= 0 && m_selectedIndex < m_shapes.size()) {
                const double sc = scale();
                const QPoint trans = imageDisplayRect().topLeft();
                const DragMode h = hitHandle(
                    QPointF(mousePos), m_shapes[m_selectedIndex], sc, trans);
                switch (h) {
                case DragMode::HandleNW:
                case DragMode::HandleSE: return Qt::SizeFDiagCursor;
                case DragMode::HandleNE:
                case DragMode::HandleSW: return Qt::SizeBDiagCursor;
                case DragMode::HandleN:
                case DragMode::HandleS:  return Qt::SizeVerCursor;
                case DragMode::HandleE:
                case DragMode::HandleW:  return Qt::SizeHorCursor;
                case DragMode::ArrowTail:
                case DragMode::ArrowHead: return Qt::SizeAllCursor;
                default: break;
                }
            }
            if (insideImage) {
                const QPoint src = dispToSource(mousePos);
                for (int i = m_shapes.size() - 1; i >= 0; --i)
                    if (hitShape(m_shapes[i], src)) return Qt::SizeAllCursor;
            }
            return Qt::ArrowCursor;
        }

        // Drawing tools: crosshair only inside the image; arrow over the
        // letterbox margin so the user knows clicks there do nothing.
        if (baseCursorFor(m_tool) == Qt::CrossCursor)
            return insideImage ? Qt::CrossCursor : Qt::ArrowCursor;
        return baseCursorFor(m_tool);
    }

    static AnnotateShapeType toShapeType(Tool t) {
        switch (t) {
        case Tool::Rect:   return AnnotateShapeType::Rect;
        case Tool::Circle: return AnnotateShapeType::Circle;
        case Tool::Arrow:  return AnnotateShapeType::Arrow;
        case Tool::Text:   return AnnotateShapeType::Text;
        case Tool::Select: return AnnotateShapeType::Rect;  // unreachable; Select handled earlier
        case Tool::None:   return AnnotateShapeType::Rect;  // unreachable; None bails earlier
        }
        return AnnotateShapeType::Rect;
    }

    bool isPendingMeaningful() const {
        if (m_pending.type == AnnotateShapeType::Arrow) {
            const QPoint d = m_pending.p2 - m_pending.p1;
            return d.manhattanLength() >= 4;
        }
        return m_pending.rect.width() >= 2 && m_pending.rect.height() >= 2;
    }

    QRect imageDisplayRect() const {
        if (m_image.isNull() || size().isEmpty()) return {};
        QSize fit = m_image.size();
        fit.scale(size(), Qt::KeepAspectRatio);
        const int x = (width()  - fit.width())  / 2;
        const int y = (height() - fit.height()) / 2;
        return QRect(QPoint(x, y), fit);
    }

    double scale() const {
        const QRect disp = imageDisplayRect();
        if (disp.isEmpty() || m_image.width() == 0) return 1.0;
        return double(disp.width()) / double(m_image.width());
    }

    QPoint dispToSource(QPoint p) const {
        const QRect disp = imageDisplayRect();
        if (disp.isEmpty()) return {};
        const double s = scale();
        const int sx = qRound((p.x() - disp.x()) / s);
        const int sy = qRound((p.y() - disp.y()) / s);
        return QPoint(std::clamp(sx, 0, m_image.width()  - 1),
                      std::clamp(sy, 0, m_image.height() - 1));
    }

    QImage                m_image;
    QList<AnnotateShape>  m_shapes;
    AnnotateShape         m_pending;
    QPoint                m_dragStartSrc;
    bool                  m_dragging = false;
    Tool                  m_tool     = Tool::Rect;
    Style                 m_style;
    int                   m_selectedIndex = -1;  // -1 = no selection

    // Select-tool drag state. m_dragStartShape is the shape's geometry at
    // mousePress; every mouseMove rebuilds from there so cursor jumps don't
    // accumulate.
    DragMode              m_dragMode = DragMode::None;
    AnnotateShape         m_dragStartShape;
    QPoint                m_dragStartMouseSrc;

    // Snapshot of m_shapes captured at the start of a destructive
    // mouse-driven operation (draw / move / resize). Engaged optional means
    // a drag is in progress; mouseRelease commits to the undo stack and
    // resets to nullopt.
    std::optional<QList<AnnotateShape>> m_pendingSnapshot;

    QList<QList<AnnotateShape>> m_undoStack;

    // Set by the dialog so it can re-sync its style controls when the
    // selected shape changes (different style than current dialog state).
    std::function<void()> m_onSelectionChanged;
};

}  // namespace

bool AnnotateAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir, ActionLogger *logger) {
    if (inputs.size() != 1) return false;

    const QString sourcePath = inputs.first();
    const QImage source = readImage(sourcePath);
    if (source.isNull()) return false;

    QDialog dlg(parent);
    dlg.setWindowTitle("Annotate");
    ActionDialogBuilder b(&dlg, inputs, /*resizable=*/true);

    const QString defaultName = QFileInfo(sourcePath).completeBaseName() + "_annotated.png";
    auto *filenameEdit = new QLineEdit(
        m_outFilename.isEmpty() ? defaultName : m_outFilename, &dlg);

    auto *canvas = new AnnotateCanvas(&dlg);
    canvas->setImage(source);

    // Style controls. Two-way binding to the canvas:
    //  - User edits a control -> canvas->setX() updates next-shape style
    //    and live-mutates the currently-selected shape (if any).
    //  - User selects a different shape -> selection-changed callback fires
    //    and repopulates the controls from the new selection (or m_style
    //    when nothing is selected). QSignalBlocker stops the repopulation
    //    from bouncing back through valueChanged signals.
    auto colorState = std::make_shared<QColor>(canvas->style().color);
    auto *colorBtn = new QPushButton(&dlg);
    auto refreshColorBtn = [colorBtn, colorState] {
        colorBtn->setText(colorState->name(QColor::HexArgb).toUpper());
        QPixmap swatch(24, 24);
        swatch.fill(*colorState);
        colorBtn->setIcon(QIcon(swatch));
    };
    refreshColorBtn();
    QObject::connect(colorBtn, &QPushButton::clicked, &dlg,
        [colorBtn, colorState, refreshColorBtn, canvas] {
            // Color picks are atomic — one snapshot before, one commit after.
            auto pre = canvas->snapshotShapes();
            const QColor c = QColorDialog::getColor(
                *colorState, colorBtn, "Shape color",
                QColorDialog::ShowAlphaChannel);
            if (!c.isValid()) return;
            *colorState = c;
            refreshColorBtn();
            canvas->setColor(c);
            canvas->pushUndo(std::move(pre));
        });

    // Slider drags fire valueChanged at every pixel — without coalescing the
    // undo stack would balloon to dozens of entries for a single slider
    // gesture. styleEditPre captures the pre-edit state on the first
    // valueChanged in a session; editingFinished commits it once the user
    // moves focus elsewhere (clicks another control, opens color picker, OKs
    // the dialog, etc.). Shared between both spinboxes so cross-spinner
    // editing within a single focus session still folds into one entry.
    auto styleEditPre = std::make_shared<std::optional<QList<AnnotateShape>>>();
    auto stashPre = [canvas, styleEditPre] {
        if (!styleEditPre->has_value())
            *styleEditPre = canvas->snapshotShapes();
    };
    auto commitPre = [canvas, styleEditPre] {
        if (styleEditPre->has_value()) {
            canvas->pushUndo(std::move(**styleEditPre));
            styleEditPre->reset();
        }
    };

    // Slider+spinbox pair: slider for quick scrubbing, spinbox for precise
    // typed values. The two stay synced via QSignalBlocker so user input on
    // either side updates the other without firing the canvas update twice.
    // canvas updates run from valueChanged of whichever the user touched.
    auto makeSliderSpin = [&dlg, stashPre, commitPre]
        (int lo, int hi, int initial, auto setOnCanvas) {
        auto *container = new QWidget(&dlg);
        auto *layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *slider = new QSlider(Qt::Horizontal, container);
        slider->setRange(lo, hi);
        slider->setValue(initial);
        auto *spin = new QSpinBox(container);
        spin->setRange(lo, hi);
        spin->setSuffix(" px");
        spin->setValue(initial);
        spin->setMaximumWidth(90);
        layout->addWidget(slider, 1);
        layout->addWidget(spin);

        QObject::connect(slider, &QSlider::valueChanged, &dlg,
            [spin, stashPre, setOnCanvas](int v) {
                { QSignalBlocker b(spin); spin->setValue(v); }
                stashPre();
                setOnCanvas(v);
            });
        QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dlg,
            [slider, stashPre, setOnCanvas](int v) {
                { QSignalBlocker b(slider); slider->setValue(v); }
                stashPre();
                setOnCanvas(v);
            });
        // Slider release ends a drag gesture; spinbox editingFinished fires
        // on focus loss or Enter. Both commit any pending style snapshot.
        QObject::connect(slider, &QSlider::sliderReleased, &dlg, commitPre);
        QObject::connect(spin, &QSpinBox::editingFinished, &dlg, commitPre);
        return std::make_tuple(container, slider, spin);
    };

    auto [strokeRow, strokeSlider, strokeSpin] = makeSliderSpin(
        1, 50, qRound(canvas->style().strokeWidth),
        [canvas](int v) { canvas->setStrokeWidth(v); });
    auto [fontRow, fontSlider, fontSpin] = makeSliderSpin(
        8, 500, canvas->style().fontPx,
        [canvas](int v) { canvas->setFontPx(v); });

    auto *fontFamilyBox = new QFontComboBox(&dlg);
    if (!canvas->style().fontFamily.isEmpty())
        fontFamilyBox->setCurrentFont(QFont(canvas->style().fontFamily));
    QObject::connect(fontFamilyBox, &QFontComboBox::currentFontChanged, &dlg,
        [canvas, stashPre, commitPre](const QFont &f) {
            // Font picks are atomic — one undo entry per selection.
            stashPre();
            canvas->setFontFamily(f.family());
            commitPre();
        });

    canvas->setSelectionChangedCallback(
        [canvas, colorState, refreshColorBtn,
         strokeSlider = strokeSlider, strokeSpin = strokeSpin,
         fontSlider   = fontSlider,   fontSpin   = fontSpin,
         fontFamilyBox] {
            QColor  color;
            double  stroke;
            int     fontPx;
            QString fontFam;
            if (const auto *sel = canvas->selectedShape()) {
                color   = sel->color;
                stroke  = sel->strokeWidth;
                fontPx  = sel->fontPx;
                fontFam = sel->fontFamily;
            } else {
                const auto &st = canvas->style();
                color   = st.color;
                stroke  = st.strokeWidth;
                fontPx  = st.fontPx;
                fontFam = st.fontFamily;
            }
            *colorState = color;
            refreshColorBtn();
            // Block valueChanged on every paired widget so this repopulation
            // doesn't re-enter canvas->setX() and trigger another paint.
            { QSignalBlocker b1(strokeSlider); QSignalBlocker b2(strokeSpin);
              strokeSlider->setValue(qRound(stroke));
              strokeSpin->setValue(qRound(stroke)); }
            { QSignalBlocker b1(fontSlider); QSignalBlocker b2(fontSpin);
              fontSlider->setValue(fontPx);
              fontSpin->setValue(fontPx); }
            { QSignalBlocker b(fontFamilyBox);
              fontFamilyBox->setCurrentFont(
                  QFont(fontFam.isEmpty() ? QFont().family() : fontFam)); }
        });

    // Toolbox: horizontal strip of QToolButtons. Non-exclusive button group
    // so clicking the active tool again toggles it off (mutual exclusivity
    // is enforced manually in the click handler below); shortcuts Alt+1..5
    // map one per tool.
    auto *toolbox = new QWidget(&dlg);
    auto *toolboxLayout = new QHBoxLayout(toolbox);
    toolboxLayout->setContentsMargins(0, 0, 0, 0);
    toolboxLayout->setSpacing(4);

    // Non-exclusive group so clicking the already-checked button can toggle
    // it off (mutual exclusivity is enforced manually in the click handler).
    // setExclusive(true) would silently swallow the second click and prevent
    // returning to a "no tool active" state.
    auto *group = new QButtonGroup(&dlg);
    group->setExclusive(false);

    struct ToolDef { Tool tool; const char *label; const char *shortcut; };
    const ToolDef defs[] = {
        { Tool::Rect,   "Rectangle", "Alt+1" },
        { Tool::Circle, "Circle",    "Alt+2" },
        { Tool::Arrow,  "Arrow",     "Alt+3" },
        { Tool::Text,   "Text",      "Alt+4" },
        { Tool::Select, "Select",    "Alt+5" },
    };
    const QColor iconColor = dlg.palette().color(QPalette::ButtonText);
    constexpr int kIconPx     = 28;
    constexpr int kBtnMinH    = 64;
    for (const auto &d : defs) {
        auto *btn = new QToolButton(toolbox);
        btn->setText(d.label);
        btn->setCheckable(true);
        btn->setIcon(QIcon(makeToolIcon(d.tool, iconColor, kIconPx)));
        btn->setIconSize(QSize(kIconPx, kIconPx));
        // Icon above label so each button reads "glyph + name" vertically;
        // combined with the taller minimum height the toolbox feels like a
        // proper tool palette instead of a thin button strip.
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setShortcut(QKeySequence(d.shortcut));
        btn->setToolTip(QString("Shortcut: %1").arg(d.shortcut));
        btn->setMinimumHeight(kBtnMinH);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // stretch=1 on every button + no trailing stretch makes the five
        // buttons split the available horizontal space evenly.
        toolboxLayout->addWidget(btn, /*stretch=*/1);
        group->addButton(btn, static_cast<int>(d.tool));
        if (d.tool == Tool::Rect) btn->setChecked(true);
    }

    QObject::connect(group, &QButtonGroup::buttonClicked, &dlg,
        [group, canvas](QAbstractButton *btn) {
            // Clicked-while-checked toggles the active tool off. The
            // exclusivity logic only runs when this click activated a tool.
            if (btn->isChecked()) {
                for (auto *other : group->buttons())
                    if (other != btn) other->setChecked(false);
                canvas->setTool(static_cast<Tool>(group->id(btn)));
            } else {
                canvas->setTool(Tool::None);
            }
        });

    // Toolbox first: tool picker reads top-to-bottom as the natural flow
    // (pick what to draw -> set its style -> choose where to save).
    b.addRow(toolbox);
    b.addRow("Color",        colorBtn);
    b.addRow("Stroke width", strokeRow);
    b.addRow("Font size",    fontRow);
    b.addRow("Font family",  fontFamilyBox);
    b.addRow("Output file",  filenameEdit);
    b.setPreview(canvas);
    b.addOutputControls(defaultOutDir, m_overwrite);

    // Ctrl+Z anywhere in the dialog. WindowShortcut context so the user can
    // undo while focus sits in the spinboxes or on a toolbox button. Spinbox
    // editingFinished commits any pending style snapshot first (Qt fires it
    // when focus leaves), so the undone state matches the user's mental
    // boundary of "one slider gesture".
    auto *undoSc = new QShortcut(QKeySequence::Undo, &dlg);
    QObject::connect(undoSc, &QShortcut::activated, &dlg,
                     [canvas, commitPre] { commitPre(); canvas->undo(); });

    b.setApplyMode([this, inputs, logger, canvas, filenameEdit, commitPre]
                   (const ActionDialogBuilder::Outcome &o) {
        const QString filename = filenameEdit->text().trimmed();
        if (filename.isEmpty()) return;
        // Flush any in-flight style snapshot so the Apply re-renders the
        // currently-displayed canvas state, not a stale one.
        commitPre();
        m_shapes      = canvas->shapes();
        m_outDir      = o.outDir;
        m_outFilename = filename;
        m_overwrite   = o.overwrite;
        apply(inputs, logger);
    });

    return b.exec().accepted;
}

QStringList AnnotateAction::apply(const QStringList &inputs, ActionLogger *logger) {
    if (logger) logger->beginRun(name(), inputs.size());

    if (inputs.size() != 1) {
        if (logger) {
            logger->error("requires exactly one input");
            logger->endRun(0, 0, 1);
        }
        return {};
    }

    const QImage source = readImage(inputs.first());
    if (source.isNull()) {
        if (logger) {
            logger->error(QString("failed to decode %1").arg(inputs.first()));
            logger->endRun(0, 0, 1);
        }
        return {};
    }

    QImage rendered = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        QPainter painter(&rendered);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        for (const auto &s : m_shapes)
            paintShape(painter, s, /*scale=*/1.0, /*translate=*/QPoint());
    }

    const auto resolved = WriteTarget::resolve(m_outDir, m_outFilename,
                                               m_overwrite, logger);
    if (resolved.status != WriteTarget::ResolveStatus::Ok) {
        if (logger) logger->endRun(/*written=*/0,
                                   /*skipped=*/resolved.status == WriteTarget::ResolveStatus::Skip ? 1 : 0,
                                   /*failed=*/ resolved.status == WriteTarget::ResolveStatus::Failed ? 1 : 0);
        return {};
    }
    QDir().mkpath(m_outDir);

    const QString finalPath = WriteTarget::write(resolved.path, logger,
        [&rendered](const QString &tempPath) {
            QImageWriter writer(tempPath);
            return writer.write(rendered);
        });
    if (finalPath.isEmpty()) {
        if (logger) logger->endRun(0, 0, 1);
        return {};
    }
    if (logger) logger->endRun(/*written=*/1, /*skipped=*/0, /*failed=*/0);
    return {finalPath};
}
