#include "gridaction.h"

#include "actionlogger.h"
#include "actionwidgets.h"
#include "imageio.h"
#include "writetarget.h"

#include <QColorDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImageWriter>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace {

// Render the title strip as fontMetrics.height() + symmetric padding so the
// strip scales naturally with the font size and the dialog never has to
// expose a "title height" knob.
int titleStripHeightFor(const QFont &font) {
    const QFontMetrics fm(font);
    const int padding = std::max(4, fm.height() / 4);
    return fm.height() + padding * 2;
}

void wireColorButton(QPushButton *btn,
                     std::shared_ptr<QColor> state,
                     const QString &title,
                     std::function<void()> onChanged) {
    auto refresh = [btn, state] {
        btn->setText(state->name(QColor::HexArgb).toUpper());
        QPixmap swatch(24, 24);
        swatch.fill(*state);
        btn->setIcon(QIcon(swatch));
    };
    refresh();
    QObject::connect(btn, &QPushButton::clicked, btn,
        [btn, state, title, refresh, onChanged] {
            const QColor c = QColorDialog::getColor(
                *state, btn, title,
                QColorDialog::ShowAlphaChannel);
            if (!c.isValid()) return;
            *state = c;
            refresh();
            onChanged();
        });
}

}  // namespace

QImage GridAction::renderGrid(const QList<QImage> &srcs,
                              const QStringList &titles,
                              int cols,
                              int cellW, int cellH,
                              int hSpacing, int vSpacing,
                              const QColor &bgColor,
                              const QColor &titleColor,
                              const QString &fontFamily,
                              int fontPointSize,
                              TitleSource titleSource) {
    if (srcs.isEmpty() || cols <= 0 || cellW <= 0 || cellH <= 0) return {};

    const int n    = srcs.size();
    const int rows = (n + cols - 1) / cols;  // ceil(n/cols)

    QFont font(fontFamily.isEmpty() ? QFont().family() : fontFamily);
    font.setPointSize(std::max(1, fontPointSize));
    const int titleH = (titleSource == TitleSource::None) ? 0
                                                          : titleStripHeightFor(font);
    const int cellTotalH = cellH + titleH;

    const int canvasW = cols * cellW + std::max(0, cols - 1) * hSpacing;
    const int canvasH = rows * cellTotalH + std::max(0, rows - 1) * vSpacing;
    if (canvasW <= 0 || canvasH <= 0) return {};

    QImage out(canvasW, canvasH, QImage::Format_ARGB32_Premultiplied);
    out.fill(bgColor);

    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing,          true);
    painter.setRenderHint(QPainter::TextAntialiasing,      true);
    painter.setFont(font);
    painter.setPen(titleColor);

    for (int i = 0; i < n; ++i) {
        const int r = i / cols;
        const int c = i % cols;
        const int x = c * (cellW + hSpacing);
        const int y = r * (cellTotalH + vSpacing);

        // Image area: [x, y, cellW, cellH]. Letterbox-fit (preserve aspect).
        const QImage &src = srcs.at(i);
        if (!src.isNull()) {
            const QImage scaled = src.scaled(cellW, cellH,
                                             Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
            const int ix = x + (cellW - scaled.width())  / 2;
            const int iy = y + (cellH - scaled.height()) / 2;
            painter.drawImage(ix, iy, scaled);
        }

        // Title strip: [x, y + cellH, cellW, titleH]. Single line, eliding so
        // wide cells stay readable and narrow cells don't wrap unpredictably
        // into the next row's space.
        if (titleH > 0 && i < titles.size()) {
            const QString text = titles.at(i);
            if (!text.isEmpty()) {
                const QRect stripRect(x, y + cellH, cellW, titleH);
                const QFontMetrics fm(font);
                const QString elided = fm.elidedText(
                    text, Qt::ElideRight, stripRect.width() - 8);
                painter.drawText(stripRect,
                                 Qt::AlignHCenter | Qt::AlignVCenter,
                                 elided);
            }
        }
    }
    painter.end();
    return out;
}

bool GridAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir, ActionLogger *logger) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Grid");

    ActionDialogBuilder b(&dlg, inputs, /*resizable=*/true);

    const int n = inputs.size();

    // Default cols = ceil(sqrt(N)) on first open; subsequent opens reuse the
    // last picked value, clamped to [1, N].
    if (m_cols <= 0) m_cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    m_cols = std::clamp(m_cols, 1, n);

    auto *colsSpin = new QSpinBox(&dlg);
    colsSpin->setRange(1, n);
    colsSpin->setValue(m_cols);

    auto *rowsLabel = new QLabel(&dlg);
    auto refreshRowsLabel = [rowsLabel, colsSpin, n] {
        const int rows = (n + colsSpin->value() - 1) / colsSpin->value();
        rowsLabel->setText(QString("%1 row%2 (auto)").arg(rows).arg(rows == 1 ? "" : "s"));
    };
    refreshRowsLabel();

    auto *cellWSpin = new QSpinBox(&dlg);
    cellWSpin->setRange(1, 100000);
    cellWSpin->setSuffix(" px");
    cellWSpin->setValue(m_cellW);

    auto *cellHSpin = new QSpinBox(&dlg);
    cellHSpin->setRange(1, 100000);
    cellHSpin->setSuffix(" px");
    cellHSpin->setValue(m_cellH);

    auto *hSpacingSpin = new QSpinBox(&dlg);
    hSpacingSpin->setRange(0, 10000);
    hSpacingSpin->setSuffix(" px");
    hSpacingSpin->setValue(m_hSpacing);

    auto *vSpacingSpin = new QSpinBox(&dlg);
    vSpacingSpin->setRange(0, 10000);
    vSpacingSpin->setSuffix(" px");
    vSpacingSpin->setValue(m_vSpacing);

    auto *fontBox = new QFontComboBox(&dlg);
    if (!m_fontFamily.isEmpty()) fontBox->setCurrentFont(QFont(m_fontFamily));

    auto *fontSizeSpin = new QSpinBox(&dlg);
    fontSizeSpin->setRange(4, 500);
    fontSizeSpin->setSuffix(" pt");
    fontSizeSpin->setValue(m_fontPointSize);

    auto *titleSrcBox = new QComboBox(&dlg);
    titleSrcBox->addItem("None",          static_cast<int>(TitleSource::None));
    titleSrcBox->addItem("Filename stem", static_cast<int>(TitleSource::Filename));
    titleSrcBox->addItem("Custom",        static_cast<int>(TitleSource::Custom));
    titleSrcBox->setCurrentIndex(titleSrcBox->findData(static_cast<int>(m_titleSource)));

    // Shared mutable color state — the QColorDialog button writes into it,
    // and the preview reads from it without owning a widget pointer.
    auto bgState    = std::make_shared<QColor>(m_bgColor);
    auto titleState = std::make_shared<QColor>(m_titleColor);

    auto *bgBtn    = new QPushButton(&dlg);
    auto *titleBtn = new QPushButton(&dlg);

    auto *filenameEdit = new QLineEdit(m_outFilename, &dlg);

    // Order + titles table. Two columns (Filename, Title). Reorder via the
    // up/down buttons next to the table — drag-drop on QTableWidget rows is
    // glitchy enough that the dedicated buttons are the dependable affordance.
    auto *table = new QTableWidget(n, 2, &dlg);
    table->setHorizontalHeaderLabels({"Filename", "Title"});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                           | QAbstractItemView::AnyKeyPressed);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setMaximumHeight(220);

    auto stemFor = [](const QString &path) {
        return QFileInfo(path).completeBaseName();
    };

    // Seed rows. Title column reuses a saved title for this exact path when
    // available, falling back to the filename stem — that way the user sees
    // something sensible even before touching the title-source combo, and
    // re-opening Grid on the same image restores prior edits.
    for (int i = 0; i < n; ++i) {
        const QString path = inputs.at(i);

        auto *fileItem = new QTableWidgetItem(QFileInfo(path).fileName());
        fileItem->setFlags(fileItem->flags() & ~Qt::ItemIsEditable);
        fileItem->setData(Qt::UserRole, path);
        table->setItem(i, 0, fileItem);

        const QString seedTitle = m_titles.value(path, stemFor(path));
        auto *titleItem = new QTableWidgetItem(seedTitle);
        table->setItem(i, 1, titleItem);
    }
    table->setColumnHidden(1, m_titleSource == TitleSource::None);

    auto orderedFromTable = [table] {
        QStringList paths;
        paths.reserve(table->rowCount());
        for (int i = 0; i < table->rowCount(); ++i)
            paths << table->item(i, 0)->data(Qt::UserRole).toString();
        return paths;
    };
    auto titlesFromTable = [table] {
        QStringList ts;
        ts.reserve(table->rowCount());
        for (int i = 0; i < table->rowCount(); ++i)
            ts << (table->item(i, 1) ? table->item(i, 1)->text() : QString());
        return ts;
    };

    auto *upBtn   = new QPushButton(QString::fromUtf8("\xE2\x86\x91"), &dlg);  // arrow up
    auto *downBtn = new QPushButton(QString::fromUtf8("\xE2\x86\x93"), &dlg);  // arrow down
    upBtn->setMaximumWidth(40);
    downBtn->setMaximumWidth(40);
    upBtn->setToolTip("Move selected up (Ctrl+Up)");
    downBtn->setToolTip("Move selected down (Ctrl+Down)");

    // Moves the currently-selected row by `delta`, preserving its contents
    // across both columns. QTableWidget has no "move row" primitive; we
    // take the items out, shift them, and reinsert. The setItem reinsert
    // would otherwise fire itemChanged and the title-source handler would
    // misread that as a user-edit and flip the combo to Custom — block
    // signals around the mutation and refresh the preview manually below.
    auto moveSelected = [table](int delta) {
        const int r = table->currentRow();
        if (r < 0) return;
        const int newRow = r + delta;
        if (newRow < 0 || newRow >= table->rowCount()) return;
        QList<QTableWidgetItem *> rowItems;
        for (int c = 0; c < table->columnCount(); ++c)
            rowItems << table->takeItem(r, c);
        {
            QSignalBlocker block(table);
            table->removeRow(r);
            table->insertRow(newRow);
            for (int c = 0; c < rowItems.size(); ++c)
                table->setItem(newRow, c, rowItems.at(c));
        }
        table->setCurrentCell(newRow, 0);
    };

    auto *upSc   = new QShortcut(QKeySequence("Ctrl+Up"),   &dlg);
    auto *downSc = new QShortcut(QKeySequence("Ctrl+Down"), &dlg);
    upSc->setContext(Qt::WidgetWithChildrenShortcut);
    downSc->setContext(Qt::WidgetWithChildrenShortcut);

    auto *tableRow = new QWidget(&dlg);
    auto *tableRowLayout = new QHBoxLayout(tableRow);
    tableRowLayout->setContentsMargins(0, 0, 0, 0);
    tableRowLayout->addWidget(table, 1);
    auto *btnCol = new QVBoxLayout;
    btnCol->setContentsMargins(0, 0, 0, 0);
    btnCol->addWidget(upBtn);
    btnCol->addWidget(downBtn);
    btnCol->addStretch();
    tableRowLayout->addLayout(btnCol);
    tableRow->setMaximumHeight(220);

    auto *previewLabel = new QLabel(&dlg);
    previewLabel->setMinimumSize(600, 400);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setStyleSheet("background-color: #444; color: #ccc;");
    previewLabel->setText("(preview)");
    previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Decoded-image cache so each preview render scales from native bytes
    // without re-decoding. Shared with the lambda chain via shared_ptr.
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

    auto currentTitleSource = [titleSrcBox] {
        return static_cast<TitleSource>(titleSrcBox->currentData().toInt());
    };

    auto refreshPreview = [=]() {
        const QStringList paths  = orderedFromTable();
        const QStringList titles = titlesFromTable();
        const QList<QImage> srcs = decodedFor(paths);

        const int cols    = colsSpin->value();
        const int cellW   = cellWSpin->value();
        const int cellH   = cellHSpin->value();
        const int hSp     = hSpacingSpin->value();
        const int vSp     = vSpacingSpin->value();
        const QString fam = fontBox->currentFont().family();
        const int fontPt  = fontSizeSpin->value();
        const auto src    = currentTitleSource();

        // Full output canvas dimensions (in real pixels). The preview
        // re-renders proportionally downscaled to fit the preview pane so
        // every parameter tweak stays interactive even for large outputs.
        const int rowsCount = (paths.size() + cols - 1) / cols;
        QFont probeFont(fam.isEmpty() ? QFont().family() : fam);
        probeFont.setPointSize(std::max(1, fontPt));
        const int titleH = (src == TitleSource::None) ? 0 : titleStripHeightFor(probeFont);
        const int cellTotalH = cellH + titleH;
        const int fullW = cols * cellW + std::max(0, cols - 1) * hSp;
        const int fullH = rowsCount * cellTotalH + std::max(0, rowsCount - 1) * vSp;
        if (fullW <= 0 || fullH <= 0) return;

        const int paneW = std::max(50, previewLabel->width());
        const int paneH = std::max(50, previewLabel->height());
        const double scale = std::min({
            1.0,
            static_cast<double>(paneW) / fullW,
            static_cast<double>(paneH) / fullH
        });

        const int pCellW = std::max(1, static_cast<int>(std::round(cellW * scale)));
        const int pCellH = std::max(1, static_cast<int>(std::round(cellH * scale)));
        const int pHSp   = static_cast<int>(std::round(hSp   * scale));
        const int pVSp   = static_cast<int>(std::round(vSp   * scale));
        const int pFontPt = std::max(1, static_cast<int>(std::round(fontPt * scale)));

        const QImage img = renderGrid(srcs, titles, cols,
                                      pCellW, pCellH, pHSp, pVSp,
                                      *bgState, *titleState,
                                      fam, pFontPt, src);
        if (img.isNull()) {
            previewLabel->setText("(preview unavailable)");
            previewLabel->setPixmap({});
            return;
        }
        previewLabel->setText({});
        previewLabel->setPixmap(QPixmap::fromImage(img));
    };

    wireColorButton(bgBtn,    bgState,    "Background color", refreshPreview);
    wireColorButton(titleBtn, titleState, "Title color",      refreshPreview);

    // Reorder buttons + Ctrl+Up / Ctrl+Down. Connected here (after
    // refreshPreview is in scope) so the preview re-renders for the new
    // order — itemChanged would have done that automatically, but moveSelected
    // suppresses it to avoid spuriously flipping the title source to Custom.
    auto moveAndRefresh = [moveSelected, refreshPreview](int delta) {
        moveSelected(delta);
        refreshPreview();
    };
    QObject::connect(upBtn,   &QPushButton::clicked,   &dlg, [moveAndRefresh] { moveAndRefresh(-1); });
    QObject::connect(downBtn, &QPushButton::clicked,   &dlg, [moveAndRefresh] { moveAndRefresh(+1); });
    QObject::connect(upSc,    &QShortcut::activated,   &dlg, [moveAndRefresh] { moveAndRefresh(-1); });
    QObject::connect(downSc,  &QShortcut::activated,   &dlg, [moveAndRefresh] { moveAndRefresh(+1); });

    // Auto-switch filename suffix to .png the moment any color picks up
    // transparency — JPEG can't carry alpha, so silently degrading on save
    // would surprise the user.
    auto syncFilenameSuffix = [filenameEdit, bgState, titleState] {
        const bool needsAlpha = bgState->alpha() < 255 || titleState->alpha() < 255;
        if (!needsAlpha) return;
        const QString name = filenameEdit->text().trimmed();
        if (name.isEmpty()) return;
        const QFileInfo fi(name);
        if (fi.suffix().compare("png", Qt::CaseInsensitive) == 0) return;
        filenameEdit->setText(fi.completeBaseName() + ".png");
    };
    // wireColorButton runs onChanged on every successful pick; we need both
    // a preview refresh and the suffix sync, so reconnect via the button's
    // clicked() signal too (fires after the lambda inside wireColorButton).
    QObject::connect(bgBtn,    &QPushButton::clicked, &dlg, syncFilenameSuffix);
    QObject::connect(titleBtn, &QPushButton::clicked, &dlg, syncFilenameSuffix);

    // Param wiring. Every change re-renders the preview; sources change also
    // mutates the table column (auto-fill stems or clear/preserve titles).
    QObject::connect(colsSpin,     qOverload<int>(&QSpinBox::valueChanged), &dlg,
                     [refreshRowsLabel, refreshPreview] { refreshRowsLabel(); refreshPreview(); });
    QObject::connect(cellWSpin,    qOverload<int>(&QSpinBox::valueChanged), &dlg, refreshPreview);
    QObject::connect(cellHSpin,    qOverload<int>(&QSpinBox::valueChanged), &dlg, refreshPreview);
    QObject::connect(hSpacingSpin, qOverload<int>(&QSpinBox::valueChanged), &dlg, refreshPreview);
    QObject::connect(vSpacingSpin, qOverload<int>(&QSpinBox::valueChanged), &dlg, refreshPreview);
    QObject::connect(fontBox,      &QFontComboBox::currentFontChanged,     &dlg,
                     [refreshPreview](const QFont &) { refreshPreview(); });
    QObject::connect(fontSizeSpin, qOverload<int>(&QSpinBox::valueChanged), &dlg, refreshPreview);
    // A user edit to a Title cell is itself a customization, so switch the
    // source combo to Custom. Filename-mode programmatic refills run inside
    // blockSignals() above, so they don't bounce through here.
    QObject::connect(table, &QTableWidget::itemChanged, &dlg,
        [titleSrcBox, refreshPreview](QTableWidgetItem *item) {
            if (item && item->column() == 1) {
                const int customIdx = titleSrcBox->findData(
                    static_cast<int>(TitleSource::Custom));
                if (customIdx >= 0 && titleSrcBox->currentIndex() != customIdx)
                    titleSrcBox->setCurrentIndex(customIdx);
            }
            refreshPreview();
        });

    QObject::connect(titleSrcBox, &QComboBox::currentIndexChanged, &dlg,
        [table, currentTitleSource, stemFor, refreshPreview] {
            const auto src = currentTitleSource();
            // Show/hide the Title column. In Filename mode also regenerate
            // every title from the path stem (overwriting prior edits) so
            // switching back from Custom restores a clean baseline.
            table->setColumnHidden(1, src == TitleSource::None);
            if (src == TitleSource::Filename) {
                table->blockSignals(true);
                for (int i = 0; i < table->rowCount(); ++i) {
                    const QString path = table->item(i, 0)->data(Qt::UserRole).toString();
                    if (auto *it = table->item(i, 1)) it->setText(QFileInfo(path).completeBaseName());
                }
                table->blockSignals(false);
            }
            refreshPreview();
        });

    // Layout. The output rows (filename + dir + overwrite policy) sit at the
    // bottom courtesy of addOutputControls running last.
    b.addRow("Columns",      colsSpin);
    b.addRow("Rows",         rowsLabel);
    b.addRow("Cell width",   cellWSpin);
    b.addRow("Cell height",  cellHSpin);
    b.addRow("H spacing",    hSpacingSpin);
    b.addRow("V spacing",    vSpacingSpin);
    b.addRow("Background",   bgBtn);
    b.addRow("Titles",       titleSrcBox);
    b.addRow("Title color",  titleBtn);
    b.addRow("Font",         fontBox);
    b.addRow("Font size",    fontSizeSpin);
    b.addRow("Order/titles", tableRow);
    b.addRow("Output file",  filenameEdit);
    b.setPreview(previewLabel, refreshPreview);
    b.addOutputControls(defaultOutDir, m_overwrite);

    refreshPreview();

    b.setApplyMode([this, inputs, logger,
                    colsSpin, cellWSpin, cellHSpin, hSpacingSpin, vSpacingSpin,
                    bgState, titleState, fontBox, fontSizeSpin,
                    currentTitleSource, orderedFromTable, table, filenameEdit]
                   (const ActionDialogBuilder::Outcome &o) {
        Q_UNUSED(inputs);
        const QString filename = filenameEdit->text().trimmed();
        if (filename.isEmpty()) return;
        m_cols          = colsSpin->value();
        m_cellW         = cellWSpin->value();
        m_cellH         = cellHSpin->value();
        m_hSpacing      = hSpacingSpin->value();
        m_vSpacing      = vSpacingSpin->value();
        m_bgColor       = *bgState;
        m_titleColor    = *titleState;
        m_fontFamily    = fontBox->currentFont().family();
        m_fontPointSize = fontSizeSpin->value();
        m_titleSource   = currentTitleSource();
        m_orderedInputs = orderedFromTable();
        // Merge each row's title into the persistent map; older paths retain
        // their saved titles so re-opening Grid on a previous input set works.
        for (int i = 0; i < table->rowCount(); ++i) {
            const QString path  = table->item(i, 0)->data(Qt::UserRole).toString();
            const QString title = table->item(i, 1) ? table->item(i, 1)->text() : QString();
            m_titles.insert(path, title);
        }
        m_outDir      = o.outDir;
        m_outFilename = filename;
        m_overwrite   = o.overwrite;
        apply(m_orderedInputs, logger);
    });

    return b.exec().accepted;
}

QStringList GridAction::apply(const QStringList &inputs, ActionLogger *logger) {
    Q_UNUSED(inputs);  // m_orderedInputs is authoritative (user may have reordered).
    if (logger) logger->beginRun(name(), m_orderedInputs.size());

    // Decode and gather titles in lock-step so a failed input drops both its
    // image and its title, keeping the renderer's title-lookup loop aligned.
    QList<QImage> srcs;
    QStringList   alignedTitles;
    srcs.reserve(m_orderedInputs.size());
    alignedTitles.reserve(m_orderedInputs.size());
    int decodeFailures = 0;
    for (const QString &p : m_orderedInputs) {
        QImage img = readImage(p);
        if (img.isNull()) {
            ++decodeFailures;
            if (logger) logger->error(QString("failed to decode %1").arg(p));
            continue;
        }
        srcs          << img;
        alignedTitles << m_titles.value(p);
    }
    if (srcs.size() < 2) {
        if (logger) {
            logger->error("nothing to grid after decode failures");
            logger->endRun(0, 0, 1);
        }
        return {};
    }

    const QImage rendered = renderGrid(srcs, alignedTitles, m_cols,
                                       m_cellW, m_cellH,
                                       m_hSpacing, m_vSpacing,
                                       m_bgColor, m_titleColor,
                                       m_fontFamily, m_fontPointSize,
                                       m_titleSource);
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
