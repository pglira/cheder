#include "concatenateaction.h"

#include "actionlogger.h"
#include "actionwidgets.h"
#include "imageio.h"
#include "writetarget.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImageWriter>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

QStringList orderedPathsFrom(QListWidget *list) {
    QStringList paths;
    paths.reserve(list->count());
    for (int i = 0; i < list->count(); ++i)
        paths << list->item(i)->data(Qt::UserRole).toString();
    return paths;
}

QColor fillFor(ConcatenateAction::Bg bg) {
    switch (bg) {
    case ConcatenateAction::Bg::White:       return QColor(255, 255, 255, 255);
    case ConcatenateAction::Bg::Black:       return QColor(  0,   0,   0, 255);
    case ConcatenateAction::Bg::Transparent: return QColor(  0,   0,   0,   0);
    }
    return Qt::white;
}

}  // namespace

QImage ConcatenateAction::renderConcat(const QList<QImage> &srcs,
                                       Orientation orient,
                                       int targetAxis,
                                       int spacing,
                                       Bg bg) {
    if (srcs.isEmpty() || targetAxis <= 0) return {};

    QList<QImage> scaled;
    scaled.reserve(srcs.size());
    for (const QImage &src : srcs) {
        if (src.isNull()) { scaled << QImage(); continue; }
        scaled << ((orient == Orientation::Horizontal)
                       ? src.scaledToHeight(targetAxis, Qt::SmoothTransformation)
                       : src.scaledToWidth (targetAxis, Qt::SmoothTransformation));
    }

    int canvasW = 0, canvasH = 0;
    int validCount = 0;
    for (const QImage &s : scaled) if (!s.isNull()) ++validCount;
    if (validCount == 0) return {};

    if (orient == Orientation::Horizontal) {
        canvasH = targetAxis;
        for (const QImage &s : scaled) if (!s.isNull()) canvasW += s.width();
        canvasW += spacing * std::max(0, validCount - 1);
    } else {
        canvasW = targetAxis;
        for (const QImage &s : scaled) if (!s.isNull()) canvasH += s.height();
        canvasH += spacing * std::max(0, validCount - 1);
    }
    if (canvasW <= 0 || canvasH <= 0) return {};

    QImage out(canvasW, canvasH, QImage::Format_ARGB32_Premultiplied);
    out.fill(fillFor(bg));

    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    int cursor = 0;
    for (const QImage &s : scaled) {
        if (s.isNull()) continue;
        if (orient == Orientation::Horizontal) {
            painter.drawImage(cursor, 0, s);
            cursor += s.width() + spacing;
        } else {
            painter.drawImage(0, cursor, s);
            cursor += s.height() + spacing;
        }
    }
    return out;
}

bool ConcatenateAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir, ActionLogger *logger) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Concatenate");

    ActionDialogBuilder b(&dlg, inputs, /*resizable=*/true);

    auto *orientBox = new QComboBox(&dlg);
    orientBox->addItem("Horizontal", static_cast<int>(Orientation::Horizontal));
    orientBox->addItem("Vertical",   static_cast<int>(Orientation::Vertical));
    orientBox->setCurrentIndex(m_orientation == Orientation::Horizontal ? 0 : 1);

    auto *targetSpin = new QSpinBox(&dlg);
    targetSpin->setRange(1, 100000);
    targetSpin->setSuffix(" px");
    targetSpin->setValue(m_targetAxis);
    auto *targetLabel = new QLabel(&dlg);

    auto refreshTargetLabel = [orientBox, targetLabel] {
        const auto orient = static_cast<Orientation>(orientBox->currentData().toInt());
        targetLabel->setText(orient == Orientation::Horizontal ? "Target height" : "Target width");
    };
    refreshTargetLabel();
    QObject::connect(orientBox, &QComboBox::currentIndexChanged, &dlg, refreshTargetLabel);

    auto *spacingSpin = new QSpinBox(&dlg);
    spacingSpin->setRange(0, 10000);
    spacingSpin->setSuffix(" px");
    spacingSpin->setValue(m_spacing);

    auto *bgBox = new QComboBox(&dlg);
    bgBox->addItem("White",       static_cast<int>(Bg::White));
    bgBox->addItem("Black",       static_cast<int>(Bg::Black));
    bgBox->addItem("Transparent", static_cast<int>(Bg::Transparent));
    bgBox->setCurrentIndex(static_cast<int>(m_bg));

    auto *inputsList = new QListWidget(&dlg);
    inputsList->setDragDropMode(QAbstractItemView::InternalMove);
    inputsList->setSelectionMode(QAbstractItemView::SingleSelection);
    inputsList->setMaximumHeight(160);
    for (const QString &p : inputs) {
        auto *item = new QListWidgetItem(QFileInfo(p).fileName());
        item->setData(Qt::UserRole, p);
        inputsList->addItem(item);
    }

    auto *upBtn   = new QPushButton(QString::fromUtf8("\xE2\x86\x91"), &dlg);  // ↑
    auto *downBtn = new QPushButton(QString::fromUtf8("\xE2\x86\x93"), &dlg);  // ↓
    upBtn->setMaximumWidth(40);
    downBtn->setMaximumWidth(40);
    upBtn->setToolTip("Move selected up");
    downBtn->setToolTip("Move selected down");

    auto moveSelected = [inputsList](int delta) {
        const int r = inputsList->currentRow();
        if (r < 0) return;
        const int newRow = r + delta;
        if (newRow < 0 || newRow >= inputsList->count()) return;
        auto *item = inputsList->takeItem(r);
        inputsList->insertItem(newRow, item);
        inputsList->setCurrentRow(newRow);
    };
    QObject::connect(upBtn,   &QPushButton::clicked, &dlg, [moveSelected] { moveSelected(-1); });
    QObject::connect(downBtn, &QPushButton::clicked, &dlg, [moveSelected] { moveSelected(+1); });

    auto *listRow = new QWidget(&dlg);
    auto *listRowLayout = new QHBoxLayout(listRow);
    listRowLayout->setContentsMargins(0, 0, 0, 0);
    // Stretch=1 so the listbox grows with the dialog; the button column stays
    // narrow at its preferred size.
    listRowLayout->addWidget(inputsList, 1);
    auto *btnCol = new QVBoxLayout;
    btnCol->setContentsMargins(0, 0, 0, 0);
    btnCol->addWidget(upBtn);
    btnCol->addWidget(downBtn);
    btnCol->addStretch();
    listRowLayout->addLayout(btnCol);
    // Cap the row so the form layout can't stretch it vertically when the
    // dialog is maximized — preview claims the slack instead.
    listRow->setMaximumHeight(160);

    auto *filenameEdit = new QLineEdit(m_outFilename, &dlg);

    auto *previewLabel = new QLabel(&dlg);
    previewLabel->setMinimumSize(600, 400);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setStyleSheet("background-color: #444; color: #ccc;");
    previewLabel->setText("(preview)");
    // Claim all the form's spare vertical (and horizontal) space — the preview
    // is the only row that benefits from extra real estate when maximized.
    previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Cache decoded inputs at native resolution so each preview render scales
    // straight from native — keeps the preview crisp as the pane grows without
    // re-reading from disk on every param tweak. Apply() decodes fresh because
    // dialog-lifetime caches don't survive the configure/apply boundary.
    auto cache = std::make_shared<QHash<QString, QImage>>();
    auto decodedFor = [cache](const QStringList &paths) {
        QList<QImage> out;
        out.reserve(paths.size());
        for (const QString &p : paths) {
            if (!cache->contains(p)) cache->insert(p, readImage(p));
            out << cache->value(p);
        }
        return out;
    };

    auto refreshPreview = [decodedFor, previewLabel, orientBox, targetSpin, spacingSpin, bgBox, inputsList]() {
        const QStringList paths = orderedPathsFrom(inputsList);
        const QList<QImage> srcs = decodedFor(paths);
        const auto orient = static_cast<Orientation>(orientBox->currentData().toInt());
        const auto bg     = static_cast<Bg>(bgBox->currentData().toInt());

        // Render at the preview pane's available axis (capped at the user's
        // target so we don't upscale beyond the actual output's resolution).
        // Spacing scales in lockstep so the miniature stays proportional.
        const int userTarget = std::max(1, targetSpin->value());
        const int paneAxis = std::max(50, (orient == Orientation::Horizontal)
                                              ? previewLabel->height()
                                              : previewLabel->width());
        const int previewTarget  = std::min(paneAxis, userTarget);
        const int previewSpacing = qRound(spacingSpin->value()
                                          * static_cast<double>(previewTarget) / userTarget);

        const QImage img = renderConcat(srcs, orient, previewTarget, previewSpacing, bg);
        if (img.isNull()) {
            previewLabel->setText("(preview unavailable)");
            previewLabel->setPixmap({});
            return;
        }
        previewLabel->setText({});
        const QPixmap pm = QPixmap::fromImage(img).scaled(previewLabel->size(),
                                                          Qt::KeepAspectRatio,
                                                          Qt::SmoothTransformation);
        previewLabel->setPixmap(pm);
    };

    // Every param change re-renders the preview. Spinners fire valueChanged on
    // each step; with cached native-res inputs this is a scale + composite,
    // cheap enough to be interactive.
    QObject::connect(orientBox,   &QComboBox::currentIndexChanged, &dlg,
                     [refreshPreview] { refreshPreview(); });
    QObject::connect(targetSpin,  qOverload<int>(&QSpinBox::valueChanged), &dlg,
                     [refreshPreview] { refreshPreview(); });
    QObject::connect(spacingSpin, qOverload<int>(&QSpinBox::valueChanged), &dlg,
                     [refreshPreview] { refreshPreview(); });
    QObject::connect(bgBox,       &QComboBox::currentIndexChanged, &dlg,
                     [refreshPreview] { refreshPreview(); });
    // Drag-drop reorder and the up/down buttons both go through remove+insert
    // on the underlying model rather than a true move, so listen to both
    // rowsMoved (in case it ever fires) and rowsInserted (which always does).
    QObject::connect(inputsList->model(), &QAbstractItemModel::rowsMoved, &dlg,
                     [refreshPreview] { refreshPreview(); });
    QObject::connect(inputsList->model(), &QAbstractItemModel::rowsInserted, &dlg,
                     [refreshPreview] { refreshPreview(); });
    // Bg→filename coupling: Transparent rewrites a non-.png filename to .png.
    // Switching back to a solid bg leaves the user's filename alone.
    QObject::connect(bgBox, &QComboBox::currentIndexChanged, &dlg, [bgBox, filenameEdit] {
        if (static_cast<Bg>(bgBox->currentData().toInt()) != Bg::Transparent) return;
        const QString name = filenameEdit->text().trimmed();
        if (name.isEmpty()) return;
        const QFileInfo fi(name);
        if (fi.suffix().compare("png", Qt::CaseInsensitive) == 0) return;
        filenameEdit->setText(fi.completeBaseName() + ".png");
    });

    b.addRow("Orientation", orientBox);
    b.addRow(targetLabel,   targetSpin);
    b.addRow("Spacing",     spacingSpin);
    b.addRow("Background",  bgBox);
    b.addRow("Order",       listRow);
    // "Output file" sits immediately above "Output directory" (added next by
    // addOutputControls), so the two output-target rows read as a pair.
    b.addRow("Output file", filenameEdit);
    b.setPreview(previewLabel, refreshPreview);
    b.addOutputControls(defaultOutDir, m_overwrite);

    refreshPreview();

    b.setApplyMode([this, inputs, logger,
                    orientBox, targetSpin, spacingSpin, bgBox, inputsList, filenameEdit]
                   (const ActionDialogBuilder::Outcome &o) {
        Q_UNUSED(inputs);  // ordering captured from inputsList below
        const QString filename = filenameEdit->text().trimmed();
        if (filename.isEmpty()) return;
        m_orientation   = static_cast<Orientation>(orientBox->currentData().toInt());
        m_targetAxis    = targetSpin->value();
        m_spacing       = spacingSpin->value();
        m_bg            = static_cast<Bg>(bgBox->currentData().toInt());
        m_orderedInputs = orderedPathsFrom(inputsList);
        m_outDir        = o.outDir;
        m_outFilename   = filename;
        m_overwrite     = o.overwrite;
        apply(m_orderedInputs, logger);
    }, logger);

    return b.exec().accepted;
}

QStringList ConcatenateAction::apply(const QStringList &inputs, ActionLogger *logger) {
    Q_UNUSED(inputs);  // m_orderedInputs is authoritative (user may have reordered).
    if (logger) logger->beginRun(name(), m_orderedInputs.size());

    QList<QImage> srcs;
    srcs.reserve(m_orderedInputs.size());
    int decodeFailures = 0;
    for (const QString &p : m_orderedInputs) {
        QImage img = readImage(p);
        if (img.isNull()) {
            ++decodeFailures;
            if (logger) logger->error(QString("failed to decode %1").arg(p));
            continue;
        }
        srcs << img;
    }
    if (srcs.size() < 2) {
        if (logger) {
            logger->error("nothing to concatenate after decode failures");
            logger->endRun(0, 0, 1);
        }
        return {};
    }

    const QImage rendered = renderConcat(srcs, m_orientation, m_targetAxis, m_spacing, m_bg);
    if (rendered.isNull()) {
        if (logger) {
            logger->error("render produced an empty image");
            logger->endRun(0, 0, 1);
        }
        return {};
    }

    const auto resolved = WriteTarget::resolve(m_outDir, m_outFilename,
                                               m_overwrite, logger);
    if (resolved.status != WriteTarget::ResolveStatus::Ok) {
        if (logger) logger->endRun(/*written=*/0,
                                   /*skipped=*/resolved.status == WriteTarget::ResolveStatus::Skip ? 1 : 0,
                                   /*failed=*/ resolved.status == WriteTarget::ResolveStatus::Failed ? 1 : 0);
        return {};
    }

    // Only now is the destination committed — don't create the output dir if
    // the resolve above ended in Skip or Failed.
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

    if (logger) logger->endRun(/*written=*/1, /*skipped=*/0,
                               /*failed=*/decodeFailures);
    return {finalPath};
}
