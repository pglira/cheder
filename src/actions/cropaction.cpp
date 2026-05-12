#include "cropaction.h"

#include "actionwidgets.h"
#include "imageio.h"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSize>
#include <QSpinBox>

#include <algorithm>
#include <cmath>

namespace {

// Hit-test pixel slop for grabbing a handle in display (widget) coordinates.
constexpr int kHandleHitSlop = 10;
// Handle square size in display coordinates.
constexpr int kHandleVisualSize = 8;

enum class DragState {
    None,
    Move,
    ResizeTL, ResizeTR, ResizeBL, ResizeBR,
    ResizeT,  ResizeB,  ResizeL,  ResizeR,
};

// Interactive crop overlay: draws an image scaled to fit the widget, with a
// rectangle drawn over the area to keep. Dragging handles resizes; dragging
// the interior moves. Optional aspect-ratio constraint pins the orthogonal
// dimension. Rect is held in source (image) coordinates so it survives
// widget resizes and listbox-driven preview swaps unchanged.
class CropPreview : public QWidget {
public:
    explicit CropPreview(QWidget *parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        setMinimumSize(400, 300);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setImage(const QImage &img) {
        m_image = img;
        update();
    }

    void setRect(const QRect &r) {
        if (r == m_rect) return;
        m_rect = r;
        update();
    }

    QRect rect() const { return m_rect; }

    // 0 = free (no aspect constraint). Otherwise width/height ratio.
    void setAspectRatio(double r) {
        m_aspect = r;
        if (m_aspect > 0.0 && !m_rect.isEmpty() && !m_image.isNull())
            setRect(snapAspectCentered(m_rect));
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        // SmoothPixmapTransform gives bilinear filtering for the
        // source-to-display rescale; without it QPainter's nearest-neighbor
        // default makes the preview crawl with stair-stepping at non-integer
        // scales.
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.fillRect(QRect(QPoint(0, 0), size()), QColor(0x22, 0x22, 0x22));
        if (m_image.isNull()) return;

        const QRect disp = imageDisplayRect();
        p.drawImage(disp, m_image);

        if (m_rect.isEmpty()) return;

        const QRect rectD = srcToDisp(m_rect);

        // Dim the image outside the crop rect by drawing four bands.
        QColor dim(0, 0, 0, 140);
        p.fillRect(QRect(disp.left(),   disp.top(),       disp.width(),                       rectD.top()    - disp.top()),    dim);
        p.fillRect(QRect(disp.left(),   rectD.bottom()+1, disp.width(),                       disp.bottom()  - rectD.bottom()), dim);
        p.fillRect(QRect(disp.left(),   rectD.top(),      rectD.left() - disp.left(),         rectD.height()),                 dim);
        p.fillRect(QRect(rectD.right()+1, rectD.top(),    disp.right() - rectD.right(),       rectD.height()),                 dim);

        // Crop rectangle outline.
        p.setPen(QPen(QColor(255, 255, 255, 220), 1));
        p.drawRect(rectD);

        // Handles.
        p.setBrush(QColor(255, 255, 255, 230));
        p.setPen(QPen(QColor(0, 0, 0, 200), 1));
        for (const QPoint &c : handleCenters(rectD))
            p.drawRect(QRect(c.x() - kHandleVisualSize/2,
                             c.y() - kHandleVisualSize/2,
                             kHandleVisualSize, kHandleVisualSize));
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (m_image.isNull() || e->button() != Qt::LeftButton) return;
        m_drag = hitTest(e->pos());
        m_dragStartMouseSrc = dispToSrc(e->pos());
        m_dragStartRect = m_rect;
        updateCursor(m_drag);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_image.isNull()) return;
        if (m_drag == DragState::None) {
            // Hover: just update cursor.
            updateCursor(hitTest(e->pos()));
            return;
        }

        const QPoint mouseSrc = dispToSrc(e->pos());
        const QPoint delta = mouseSrc - m_dragStartMouseSrc;

        QRect r = m_dragStartRect;
        switch (m_drag) {
        case DragState::None: return;
        case DragState::Move:     r.translate(delta);                             clampMove(r);            break;
        case DragState::ResizeTL: resizeCorner(r, r.bottomRight(), QPoint(m_dragStartRect.left()+delta.x(), m_dragStartRect.top()+delta.y())); break;
        case DragState::ResizeTR: resizeCorner(r, r.bottomLeft(),  QPoint(m_dragStartRect.right()+delta.x(),m_dragStartRect.top()+delta.y())); break;
        case DragState::ResizeBL: resizeCorner(r, r.topRight(),    QPoint(m_dragStartRect.left()+delta.x(), m_dragStartRect.bottom()+delta.y())); break;
        case DragState::ResizeBR: resizeCorner(r, r.topLeft(),     QPoint(m_dragStartRect.right()+delta.x(),m_dragStartRect.bottom()+delta.y())); break;
        case DragState::ResizeT:  resizeEdge(r, Qt::TopEdge,    m_dragStartRect.top()    + delta.y());      break;
        case DragState::ResizeB:  resizeEdge(r, Qt::BottomEdge, m_dragStartRect.bottom() + delta.y());      break;
        case DragState::ResizeL:  resizeEdge(r, Qt::LeftEdge,   m_dragStartRect.left()   + delta.x());      break;
        case DragState::ResizeR:  resizeEdge(r, Qt::RightEdge,  m_dragStartRect.right()  + delta.x());      break;
        }
        setRect(r.normalized());
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        Q_UNUSED(e);
        m_drag = DragState::None;
        setCursor(Qt::ArrowCursor);
    }

    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        update();
    }

private:
    QRect imageDisplayRect() const {
        if (m_image.isNull()) return {};
        const QSize ws = size();
        const QSize is = m_image.size();
        const double sw = static_cast<double>(ws.width())  / is.width();
        const double sh = static_cast<double>(ws.height()) / is.height();
        const double s  = std::min(sw, sh);
        const int dw = static_cast<int>(is.width()  * s);
        const int dh = static_cast<int>(is.height() * s);
        return QRect((ws.width()  - dw) / 2, (ws.height() - dh) / 2, dw, dh);
    }

    QPoint srcToDisp(const QPoint &p) const {
        const QRect disp = imageDisplayRect();
        const double sx = static_cast<double>(disp.width())  / m_image.width();
        const double sy = static_cast<double>(disp.height()) / m_image.height();
        return QPoint(disp.left() + static_cast<int>(p.x() * sx),
                      disp.top()  + static_cast<int>(p.y() * sy));
    }
    QRect srcToDisp(const QRect &r) const {
        return QRect(srcToDisp(r.topLeft()), srcToDisp(r.bottomRight()));
    }
    QPoint dispToSrc(const QPoint &p) const {
        const QRect disp = imageDisplayRect();
        if (disp.isEmpty()) return {};
        const double sx = static_cast<double>(m_image.width())  / disp.width();
        const double sy = static_cast<double>(m_image.height()) / disp.height();
        return QPoint(static_cast<int>((p.x() - disp.left()) * sx),
                      static_cast<int>((p.y() - disp.top())  * sy));
    }

    QList<QPoint> handleCenters(const QRect &rD) const {
        return {
            rD.topLeft(), rD.topRight(), rD.bottomLeft(), rD.bottomRight(),
            QPoint(rD.center().x(), rD.top()),
            QPoint(rD.center().x(), rD.bottom()),
            QPoint(rD.left(),  rD.center().y()),
            QPoint(rD.right(), rD.center().y()),
        };
    }

    DragState hitTest(const QPoint &dispPt) const {
        if (m_image.isNull() || m_rect.isEmpty()) return DragState::None;
        const QRect rD = srcToDisp(m_rect);
        auto near = [&](const QPoint &c) {
            return std::abs(dispPt.x() - c.x()) <= kHandleHitSlop
                && std::abs(dispPt.y() - c.y()) <= kHandleHitSlop;
        };
        if (near(rD.topLeft()))     return DragState::ResizeTL;
        if (near(rD.topRight()))    return DragState::ResizeTR;
        if (near(rD.bottomLeft()))  return DragState::ResizeBL;
        if (near(rD.bottomRight())) return DragState::ResizeBR;
        if (near(QPoint(rD.center().x(), rD.top())))    return DragState::ResizeT;
        if (near(QPoint(rD.center().x(), rD.bottom()))) return DragState::ResizeB;
        if (near(QPoint(rD.left(),  rD.center().y())))  return DragState::ResizeL;
        if (near(QPoint(rD.right(), rD.center().y())))  return DragState::ResizeR;
        if (rD.contains(dispPt))    return DragState::Move;
        return DragState::None;
    }

    void updateCursor(DragState s) {
        switch (s) {
        case DragState::None:                                setCursor(Qt::ArrowCursor); break;
        case DragState::Move:                                setCursor(Qt::SizeAllCursor); break;
        case DragState::ResizeTL: case DragState::ResizeBR:  setCursor(Qt::SizeFDiagCursor); break;
        case DragState::ResizeTR: case DragState::ResizeBL:  setCursor(Qt::SizeBDiagCursor); break;
        case DragState::ResizeT:  case DragState::ResizeB:   setCursor(Qt::SizeVerCursor); break;
        case DragState::ResizeL:  case DragState::ResizeR:   setCursor(Qt::SizeHorCursor); break;
        }
    }

    void clampMove(QRect &r) const {
        const QSize is = m_image.size();
        if (r.left()   < 0)             r.translate(-r.left(), 0);
        if (r.top()    < 0)             r.translate(0, -r.top());
        if (r.right()  >= is.width())   r.translate(is.width()  - 1 - r.right(),  0);
        if (r.bottom() >= is.height())  r.translate(0, is.height() - 1 - r.bottom());
    }

    // Resize from a corner: `pin` stays put; `freeSrc` is the dragged corner's
    // target. With aspect constraint, the wider/taller axis shrinks so the
    // resulting rect matches m_aspect.
    void resizeCorner(QRect &r, const QPoint &pin, QPoint freeSrc) {
        clampToImage(freeSrc);
        QRect raw = QRect(pin, freeSrc).normalized();
        if (m_aspect > 0.0) raw = snapAspectPinned(raw, pin);
        if (raw.width() < 1)  raw.setWidth(1);
        if (raw.height() < 1) raw.setHeight(1);
        r = raw;
    }

    // Resize one edge: keep the opposite edge pinned. With aspect constraint,
    // the orthogonal dimension adjusts about the rect's horizontal/vertical
    // center (so the rect doesn't slide sideways as a side gets dragged).
    void resizeEdge(QRect &r, Qt::Edge edge, int srcCoord) {
        const QSize is = m_image.size();
        switch (edge) {
        case Qt::TopEdge:    r.setTop(std::clamp(srcCoord, 0, r.bottom() - 1));   break;
        case Qt::BottomEdge: r.setBottom(std::clamp(srcCoord, r.top() + 1, is.height() - 1)); break;
        case Qt::LeftEdge:   r.setLeft(std::clamp(srcCoord, 0, r.right() - 1));   break;
        case Qt::RightEdge:  r.setRight(std::clamp(srcCoord, r.left() + 1, is.width() - 1));  break;
        }
        if (m_aspect > 0.0) {
            if (edge == Qt::TopEdge || edge == Qt::BottomEdge) {
                const int h = r.height();
                const int w = std::max(1, static_cast<int>(std::round(h * m_aspect)));
                const int cx = m_dragStartRect.center().x();
                r.setLeft (std::max(0,                cx - w / 2));
                r.setRight(std::min(is.width()  - 1, r.left() + w - 1));
            } else {
                const int w = r.width();
                const int h = std::max(1, static_cast<int>(std::round(w / m_aspect)));
                const int cy = m_dragStartRect.center().y();
                r.setTop   (std::max(0,                cy - h / 2));
                r.setBottom(std::min(is.height() - 1, r.top() + h - 1));
            }
        }
    }

    void clampToImage(QPoint &p) const {
        const QSize is = m_image.size();
        p.setX(std::clamp(p.x(), 0, is.width()  - 1));
        p.setY(std::clamp(p.y(), 0, is.height() - 1));
    }

    // Shrink `r` to match m_aspect by reducing the long axis. Returns the
    // (w, h) that fits inside `r` while matching the constraint. Callers
    // decide where to place those dimensions.
    QSize sizeForAspect(const QRect &r) const {
        const double current = static_cast<double>(r.width()) / std::max(1, r.height());
        int w = r.width(), h = r.height();
        if (current > m_aspect) w = static_cast<int>(std::round(h * m_aspect));
        else                    h = static_cast<int>(std::round(w / m_aspect));
        return QSize(std::max(1, w), std::max(1, h));
    }

    // Snap `r` to m_aspect, centered on its own center. Used when the user
    // picks a new aspect from the combo — keep the framing the user already
    // had, just enforce the ratio.
    QRect snapAspectCentered(const QRect &r) const {
        const QSize s = sizeForAspect(r);
        return QRect(r.center().x() - s.width()  / 2,
                     r.center().y() - s.height() / 2,
                     s.width(), s.height());
    }

    // Snap `r` to m_aspect, keeping `pin` (which must be one of `r`'s four
    // corners) fixed. Used during corner-handle drag so the pinned corner
    // doesn't move while the dragged corner snaps along the aspect diagonal.
    QRect snapAspectPinned(const QRect &r, const QPoint &pin) const {
        const QSize s = sizeForAspect(r);
        const bool leftPin = pin.x() <= r.center().x();
        const bool topPin  = pin.y() <= r.center().y();
        const int x = leftPin ? pin.x() : pin.x() - s.width()  + 1;
        const int y = topPin  ? pin.y() : pin.y() - s.height() + 1;
        return QRect(x, y, s.width(), s.height());
    }

    QImage     m_image;
    QRect      m_rect;
    double     m_aspect = 0.0;
    DragState  m_drag = DragState::None;
    QPoint     m_dragStartMouseSrc;
    QRect      m_dragStartRect;
};

}  // namespace

bool CropAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir, ActionLogger *logger) {
    // Same-size validation. Refuses the entire selection on any mismatch —
    // the dialog never opens, so the user can see at a glance what went wrong
    // and re-select.
    if (inputs.isEmpty()) return false;
    const QSize ref = peekImageSize(inputs.first());
    if (!ref.isValid()) {
        QMessageBox::warning(parent, "Crop",
            QString("Could not read image dimensions for %1").arg(inputs.first()));
        return false;
    }
    QStringList mismatched;
    for (int i = 1; i < inputs.size(); ++i) {
        const QSize s = peekImageSize(inputs[i]);
        if (s != ref) mismatched << QFileInfo(inputs[i]).fileName();
    }
    if (!mismatched.isEmpty()) {
        QMessageBox::warning(parent, "Crop",
            QString("Crop requires images of identical dimensions.\n"
                    "Reference (%1×%2): %3\n"
                    "Mismatched: %4")
                .arg(ref.width()).arg(ref.height())
                .arg(QFileInfo(inputs.first()).fileName())
                .arg(mismatched.join(", ")));
        return false;
    }

    QDialog dlg(parent);
    dlg.setWindowTitle("Crop");
    ActionDialogBuilder b(&dlg, inputs, /*resizable=*/true);

    auto *aspectBox = new QComboBox(&dlg);
    aspectBox->addItem("Free",   0.0);
    aspectBox->addItem("1:1",    1.0);
    aspectBox->addItem("3:2",    3.0 / 2.0);
    aspectBox->addItem("4:3",    4.0 / 3.0);
    aspectBox->addItem("16:9",   16.0 / 9.0);
    aspectBox->addItem("9:16",   9.0 / 16.0);
    aspectBox->addItem("Custom", -1.0);

    auto *customRow = new QWidget(&dlg);
    auto *customLayout = new QHBoxLayout(customRow);
    customLayout->setContentsMargins(0, 0, 0, 0);
    auto *wSpin = new QSpinBox(&dlg); wSpin->setRange(1, 1000); wSpin->setValue(3);
    auto *colon = new QLabel(":", &dlg);
    auto *hSpin = new QSpinBox(&dlg); hSpin->setRange(1, 1000); hSpin->setValue(2);
    customLayout->addWidget(wSpin);
    customLayout->addWidget(colon);
    customLayout->addWidget(hSpin);
    customLayout->addStretch();
    auto *customLabel = new QLabel("Custom ratio", &dlg);
    customLabel->setVisible(false);
    customRow->setVisible(false);

    // Only built when there's something to select between; for a single
    // input the dialog skips the listbox entirely (constructing one and
    // parenting it to the dialog without adding it to a layout would render
    // it as a stray widget at (0,0) once the dialog opens maximized).
    QListWidget *inputsList = nullptr;
    if (inputs.size() > 1) {
        inputsList = new QListWidget(&dlg);
        inputsList->setSelectionMode(QAbstractItemView::SingleSelection);
        for (const QString &p : inputs) {
            auto *item = new QListWidgetItem(QFileInfo(p).fileName());
            item->setData(Qt::UserRole, p);
            inputsList->addItem(item);
        }
        inputsList->setCurrentRow(0);
        inputsList->setMaximumHeight(140);
    }

    auto *preview = new CropPreview(&dlg);
    const QImage first = readImage(inputs.first());
    preview->setImage(first);
    preview->setRect(QRect(0, 0, ref.width(), ref.height()));

    auto applyAspect = [aspectBox, wSpin, hSpin, preview, customRow, customLabel]() {
        const double v = aspectBox->currentData().toDouble();
        const bool isCustom = (v < 0.0);
        customLabel->setVisible(isCustom);
        customRow->setVisible(isCustom);
        if (isCustom) {
            const double w = std::max(1, wSpin->value());
            const double h = std::max(1, hSpin->value());
            preview->setAspectRatio(w / h);
        } else {
            preview->setAspectRatio(v);
        }
    };
    QObject::connect(aspectBox, &QComboBox::currentIndexChanged, &dlg, applyAspect);
    QObject::connect(wSpin, qOverload<int>(&QSpinBox::valueChanged), &dlg, applyAspect);
    QObject::connect(hSpin, qOverload<int>(&QSpinBox::valueChanged), &dlg, applyAspect);

    if (inputsList) {
        QObject::connect(inputsList, &QListWidget::currentItemChanged, &dlg,
                         [preview](QListWidgetItem *cur, QListWidgetItem *) {
            if (!cur) return;
            preview->setImage(readImage(cur->data(Qt::UserRole).toString()));
        });
    }

    b.addRow("Aspect", aspectBox);
    b.addRow(customLabel, customRow);
    if (inputsList) b.addRow("Image", inputsList);
    b.setPreview(preview);
    b.addOutputControls(defaultOutDir, m_overwrite);

    const auto r = b.exec();
    if (!r.accepted) return false;
    const QRect rect = preview->rect();
    if (rect.isEmpty() || rect.width() < 1 || rect.height() < 1) return false;

    m_rect      = rect;
    m_outDir    = r.outDir;
    m_overwrite = r.overwrite;
    return true;
}

QString CropAction::applyOne(const QString &input, ActionLogger *logger) {
    QImage img = readImage(input);
    if (img.isNull()) return {};

    // Same-size guarantee was enforced in configure(); apply() trusts it.
    // Belt-and-braces clamp anyway so a file change between configure and
    // apply can't crash.
    QRect r = m_rect.intersected(img.rect());
    if (r.isEmpty()) return {};

    const QImage cropped = img.copy(r);

    return writeOne(input, logger, [&](const QString &temp) {
        QImageWriter w(temp);
        return w.write(cropped);
    });
}
