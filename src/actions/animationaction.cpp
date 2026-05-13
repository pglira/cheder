#include "animationaction.h"

#include "actionlogger.h"
#include "actionwidgets.h"
#include "imageio.h"
#include "writetarget.h"

#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QResizeEvent>
#include <QSize>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace {

// Cap on how many frames the dialog decodes for live playback. Animation
// inputs can run into the thousands; decoding them all to memory would be
// wasteful. The preview just needs enough frames to give the user a feel for
// timing — looping the first N at the configured FPS is plenty.
constexpr int kPreviewFrameCap = 100;

// Preview frames are scaled down on decode to bound memory. The actual encode
// uses the inputs at native resolution; this only affects what the live
// preview pane displays.
constexpr int kPreviewFrameMaxEdge = 800;

// QLabel subclass that paints a held QImage scaled-to-fit the label's
// current size, so the form layout can resize the preview pane without
// clipping. Used by the animation playback timer.
class FrameLabel : public QLabel {
public:
    using QLabel::QLabel;
    void setFrame(const QImage &img) { m_frame = img; rescale(); }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QLabel::resizeEvent(e);
        rescale();
    }

private:
    void rescale() {
        if (m_frame.isNull() || size().isEmpty()) return;
        setPixmap(QPixmap::fromImage(m_frame).scaled(
            size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    QImage m_frame;
};

QString crfForQuality(AnimationAction::Quality q) {
    switch (q) {
    case AnimationAction::Quality::Low:    return "28";
    case AnimationAction::Quality::Medium: return "23";
    case AnimationAction::Quality::High:   return "18";
    }
    return "23";
}

// Writes a ffmpeg concat-demuxer list pointing at each input path, with a
// `duration` line per entry so the demuxer holds each image for one frame
// at the configured FPS. The final file is repeated without a duration to
// satisfy the documented concat quirk (the last entry's duration is
// otherwise ignored).
bool writeConcatList(const QString &listPath, const QStringList &inputs, double frameDuration) {
    QFile f(listPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);

    auto escape = [](const QString &p) {
        // ffmpeg concat: wrap in single quotes; an embedded single quote
        // closes the quoted string, then is concatenated as '\''.
        QString s = p;
        s.replace('\'', QStringLiteral("'\\''"));
        return s;
    };

    for (const QString &p : inputs) {
        out << "file '" << escape(p) << "'\n";
        out << "duration " << QString::number(frameDuration, 'f', 6) << "\n";
    }
    if (!inputs.isEmpty())
        out << "file '" << escape(inputs.last()) << "'\n";
    return true;
}

}  // namespace

bool AnimationAction::configure(QWidget *parent, const QStringList &inputs, const QString &defaultOutDir, ActionLogger *logger) {
    if (inputs.size() < 2) return false;

    // Same-size validation — refuse the entire selection on any mismatch so
    // the user can see what went wrong before the dialog opens. Mirrors Crop.
    const QSize ref = peekImageSize(inputs.first());
    if (!ref.isValid()) {
        QMessageBox::warning(parent, "Animation",
            QString("Could not read image dimensions for %1").arg(inputs.first()));
        return false;
    }
    QStringList mismatched;
    for (int i = 1; i < inputs.size(); ++i) {
        const QSize s = peekImageSize(inputs[i]);
        if (s != ref) mismatched << QFileInfo(inputs[i]).fileName();
    }
    if (!mismatched.isEmpty()) {
        QMessageBox::warning(parent, "Animation",
            QString("Animation requires images of identical dimensions.\n"
                    "Reference (%1×%2): %3\n"
                    "Mismatched: %4")
                .arg(ref.width()).arg(ref.height())
                .arg(QFileInfo(inputs.first()).fileName())
                .arg(mismatched.join(", ")));
        return false;
    }

    QDialog dlg(parent);
    dlg.setWindowTitle("Animation");

    ActionDialogBuilder b(&dlg, inputs, /*resizable=*/true);

    auto *formatBox = new QComboBox(&dlg);
    formatBox->addItem("GIF",         static_cast<int>(Format::Gif));
    formatBox->addItem("MP4 (H.264)", static_cast<int>(Format::Mp4));
    formatBox->setCurrentIndex(formatBox->findData(static_cast<int>(m_format)));

    auto *fpsSpin = new QSpinBox(&dlg);
    fpsSpin->setRange(1, 120);
    fpsSpin->setSuffix(" fps");
    fpsSpin->setValue(m_fps);

    auto *loopsLabel = new QLabel("Loops", &dlg);
    auto *loopsSpin  = new QSpinBox(&dlg);
    loopsSpin->setRange(0, 99);
    loopsSpin->setValue(m_loops);
    loopsSpin->setSpecialValueText("0 (infinite)");

    auto *qualityLabel = new QLabel("Quality", &dlg);
    auto *qualityBox   = new QComboBox(&dlg);
    qualityBox->addItem("Low (smaller file)",      static_cast<int>(Quality::Low));
    qualityBox->addItem("Medium",                  static_cast<int>(Quality::Medium));
    qualityBox->addItem("High (visually lossless)", static_cast<int>(Quality::High));
    qualityBox->setCurrentIndex(qualityBox->findData(static_cast<int>(m_quality)));

    auto *filenameEdit = new QLineEdit(m_outFilename, &dlg);

    auto *previewLabel = new FrameLabel(&dlg);
    previewLabel->setMinimumSize(600, 400);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setStyleSheet("background-color: #444; color: #ccc;");
    previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *previewStatus = new QLabel(&dlg);
    previewStatus->setAlignment(Qt::AlignCenter);
    previewStatus->setStyleSheet("color: #888;");

    const int previewCount = std::min<int>(inputs.size(), kPreviewFrameCap);
    auto cache    = std::make_shared<QList<QImage>>();
    auto frameIdx = std::make_shared<int>(0);

    // Asynchronous chunked decode of the first `previewCount` frames: a
    // repeating QTimer fires once per event-loop tick, decodes one frame,
    // and stops itself when done. The dialog stays responsive even for
    // thousands of frames; the play timer renders from whatever's already
    // cached. inputs is captured by reference — it lives for the duration of
    // configure() (and therefore the dialog's event loop).
    auto *decodeTimer = new QTimer(&dlg);
    decodeTimer->setInterval(0);
    auto updateStatus = [previewStatus, cache, previewCount, n = inputs.size()] {
        const int loaded = cache->size();
        if (loaded < previewCount)
            previewStatus->setText(QString("Loading preview… %1 / %2 frames").arg(loaded).arg(previewCount));
        else if (previewCount < n)
            previewStatus->setText(QString("Preview: first %1 of %2 frames").arg(previewCount).arg(n));
        else
            previewStatus->setText(QString("Preview: %1 frames").arg(n));
    };
    updateStatus();

    QObject::connect(decodeTimer, &QTimer::timeout, &dlg,
        [cache, &inputs, previewCount, updateStatus, decodeTimer] {
            const int next = cache->size();
            if (next >= previewCount) { decodeTimer->stop(); return; }
            QImage img = readImage(inputs.at(next));
            if (!img.isNull()
                    && (img.width() > kPreviewFrameMaxEdge || img.height() > kPreviewFrameMaxEdge)) {
                img = img.scaled(kPreviewFrameMaxEdge, kPreviewFrameMaxEdge,
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            cache->append(img);
            updateStatus();
        });
    decodeTimer->start();

    auto *playTimer = new QTimer(&dlg);
    auto tick = [cache, frameIdx, previewLabel] {
        if (cache->isEmpty()) return;
        const int idx = (*frameIdx) % cache->size();
        previewLabel->setFrame((*cache)[idx]);
        ++(*frameIdx);
    };
    QObject::connect(playTimer, &QTimer::timeout, &dlg, tick);
    playTimer->start(std::max(1, 1000 / fpsSpin->value()));

    auto applyFormatVisibility = [=] {
        const auto fmt = static_cast<Format>(formatBox->currentData().toInt());
        const bool isGif = (fmt == Format::Gif);
        loopsLabel->setVisible(isGif);
        loopsSpin->setVisible(isGif);
        qualityLabel->setVisible(!isGif);
        qualityBox->setVisible(!isGif);

        // Auto-switch filename suffix to match the chosen format.
        const QString want = isGif ? "gif" : "mp4";
        const QString name = filenameEdit->text().trimmed();
        if (!name.isEmpty()) {
            const QFileInfo fi(name);
            if (fi.suffix().compare(want, Qt::CaseInsensitive) != 0)
                filenameEdit->setText(fi.completeBaseName() + "." + want);
        }
    };
    applyFormatVisibility();

    QObject::connect(formatBox, &QComboBox::currentIndexChanged, &dlg, applyFormatVisibility);
    QObject::connect(fpsSpin, qOverload<int>(&QSpinBox::valueChanged), &dlg,
        [playTimer](int v) { playTimer->setInterval(std::max(1, 1000 / std::max(1, v))); });

    b.addRow("Format",       formatBox);
    b.addRow("FPS",          fpsSpin);
    b.addRow(loopsLabel,     loopsSpin);
    b.addRow(qualityLabel,   qualityBox);
    b.addRow("Output file",  filenameEdit);

    auto *previewContainer = new QWidget(&dlg);
    auto *previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->addWidget(previewLabel, 1);
    previewLayout->addWidget(previewStatus);
    b.setPreview(previewContainer);
    b.addOutputControls(defaultOutDir, m_overwrite);

    b.setApplyMode([this, inputs, logger,
                    formatBox, fpsSpin, loopsSpin, qualityBox, filenameEdit]
                   (const ActionDialogBuilder::Outcome &o) {
        const QString filename = filenameEdit->text().trimmed();
        if (filename.isEmpty()) return;
        m_format      = static_cast<Format>(formatBox->currentData().toInt());
        m_fps         = fpsSpin->value();
        m_loops       = loopsSpin->value();
        m_quality     = static_cast<Quality>(qualityBox->currentData().toInt());
        m_outDir      = o.outDir;
        m_outFilename = filename;
        m_overwrite   = o.overwrite;
        apply(inputs, logger);
    }, logger);

    return b.exec().accepted;
}

QStringList AnimationAction::apply(const QStringList &inputs, ActionLogger *logger) {
    if (logger) logger->beginRun(name(), inputs.size());

    const auto resolved = WriteTarget::resolve(m_outDir, m_outFilename, m_overwrite, logger);
    if (resolved.status != WriteTarget::ResolveStatus::Ok) {
        if (logger) logger->endRun(/*written=*/0,
                                   /*skipped=*/resolved.status == WriteTarget::ResolveStatus::Skip ? 1 : 0,
                                   /*failed=*/ resolved.status == WriteTarget::ResolveStatus::Failed ? 1 : 0);
        return {};
    }
    QDir().mkpath(m_outDir);

    if (logger) logger->info(QString("encoding %1 frames at %2 fps -> %3")
                                 .arg(inputs.size()).arg(m_fps)
                                 .arg(m_format == Format::Gif ? "GIF" : "MP4"));

    // Temp dir holds the concat-demuxer list file. QTemporaryDir auto-cleans
    // when it falls out of scope; ffmpeg has finished by then.
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (logger) {
            logger->error("could not create temp dir for ffmpeg list");
            logger->endRun(0, 0, 1);
        }
        return {};
    }
    const QString listPath = tempDir.path() + "/concat.txt";
    const double frameDuration = 1.0 / std::max(1, m_fps);
    if (!writeConcatList(listPath, inputs, frameDuration)) {
        if (logger) {
            logger->error("could not write ffmpeg concat list");
            logger->endRun(0, 0, 1);
        }
        return {};
    }

    const QString finalPath = WriteTarget::write(resolved.path, logger,
        [&](const QString &tempPath) {
            QStringList args;
            args << "-y"
                 << "-f"     << "concat"
                 << "-safe"  << "0"
                 << "-i"     << listPath;
            if (m_format == Format::Gif) {
                // Single-pass palette generation + use for best quality
                // without a separate palettegen invocation.
                args << "-vf"
                     << QString("fps=%1,split[a][b];[a]palettegen=stats_mode=full[p];[b][p]paletteuse=dither=sierra2_4a")
                            .arg(m_fps)
                     << "-loop" << QString::number(m_loops);
            } else {
                // libx264 with yuv420p requires both dimensions divisible by
                // 2 (chroma subsampling); round odd inputs down to the next
                // even number rather than failing the encode. Loses at most
                // one pixel per axis — invisible vs the libx264 error wall.
                args << "-vf"      << QString("fps=%1,scale=trunc(iw/2)*2:trunc(ih/2)*2").arg(m_fps)
                     << "-c:v"     << "libx264"
                     << "-crf"     << crfForQuality(m_quality)
                     << "-pix_fmt" << "yuv420p";
            }
            args << tempPath;

            QProcess proc;
            proc.setProcessChannelMode(QProcess::MergedChannels);
            proc.start("ffmpeg", args);
            if (!proc.waitForStarted(3000)) {
                if (logger) logger->error("ffmpeg failed to start (is it installed?)");
                return false;
            }
            // Pump events while ffmpeg runs so the rest of the UI stays
            // responsive on long encodes — QProcess::waitForFinished blocks
            // the calling thread without spinning the event loop, which would
            // otherwise freeze the window for the whole encode duration.
            QEventLoop loop;
            QObject::connect(&proc, &QProcess::finished, &loop, &QEventLoop::quit);
            if (proc.state() != QProcess::NotRunning) loop.exec();

            if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
                if (logger) {
                    const QString tail = QString::fromLocal8Bit(proc.readAll()).right(800);
                    logger->error(QString("ffmpeg exited with %1: %2")
                                      .arg(proc.exitCode()).arg(tail.trimmed()));
                }
                return false;
            }
            return true;
        });

    if (finalPath.isEmpty()) {
        if (logger) logger->endRun(0, 0, 1);
        return {};
    }
    if (logger) logger->endRun(/*written=*/1, /*skipped=*/0, /*failed=*/0);
    return {finalPath};
}
