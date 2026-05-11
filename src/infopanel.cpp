#include "infopanel.h"

#include "metadatareader.h"

#include <QDateTime>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageReader>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {

constexpr int kPanelMargin = 8;

QString formatBytes(qint64 bytes) {
    return QLocale::system().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

// Pull a string-valued field from a QJsonObject regardless of whether the
// underlying JSON value is a string or a number (exiftool emits both).
QString jsonField(const QJsonObject &obj, const char *key) {
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble());
    return {};
}

}  // namespace

InfoPanel::InfoPanel(QWidget *parent)
    : QWidget(parent), m_reader(new MetadataReader(this)) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(kPanelMargin, kPanelMargin, kPanelMargin, kPanelMargin);
    root->setSpacing(kPanelMargin);

    auto *fields = new QWidget(this);
    m_form = new QFormLayout(fields);
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setLabelAlignment(Qt::AlignRight);
    root->addWidget(fields);

    root->addStretch();

    connect(m_reader, &MetadataReader::metadataReady, this, &InfoPanel::onMetadataReady);
}

void InfoPanel::setCurrentPath(const QString &path) {
    // Skip if neither the path nor visibility implies fresh work.
    if (path == m_currentPath && isVisible()) return;
    m_currentPath = path;
    // Hidden: defer; the next showEvent will refresh against the stored path
    // (so we don't spawn exiftool while the panel isn't on screen).
    if (isVisible()) refresh();
}

void InfoPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refresh();
}

void InfoPanel::refresh() {
    clearForm();

    if (m_currentPath.isEmpty() || !QFileInfo::exists(m_currentPath)) {
        addRow("Status", "No selection");
        return;
    }

    populateBasics(m_currentPath);
    m_reader->request(m_currentPath);
}

void InfoPanel::clearForm() {
    while (m_form->rowCount() > 0) m_form->removeRow(0);
}

void InfoPanel::addRow(const QString &label, const QString &value) {
    if (value.isEmpty()) return;
    auto *v = new QLabel(value, this);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->setWordWrap(true);
    m_form->addRow(label + ":", v);
}

void InfoPanel::populateBasics(const QString &path) {
    const QFileInfo fi(path);

    QSize dims;
    QString format;
    {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        dims = reader.size();
        format = QString::fromLatin1(reader.format()).toUpper();
    }

    addRow("Name",     fi.fileName());
    addRow("Folder",   fi.absolutePath());
    addRow("Format",   format);
    if (dims.isValid())
        addRow("Dimensions", QString("%1 × %2 px").arg(dims.width()).arg(dims.height()));
    addRow("Size",     formatBytes(fi.size()));
    addRow("Modified", fi.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
}

void InfoPanel::onMetadataReady(const QString &path, const QJsonObject &obj) {
    if (path != m_currentPath) return;  // stale — selection moved on

    auto pushIf = [this, &obj](const QString &label, const char *key) {
        addRow(label, jsonField(obj, key));
    };

    const QString make  = jsonField(obj, "Make");
    const QString model = jsonField(obj, "Model");
    const QString camera = (make.isEmpty() ? model : (make + " " + model)).trimmed();

    // Only add a separator if at least one EXIF row will be emitted.
    auto hasAnyExif = [&] {
        static const char *keys[] = {
            "Make", "Model", "LensModel", "ExposureTime", "FNumber", "ISO",
            "FocalLength", "DateTimeOriginal", "GPSLatitude", "GPSLongitude"
        };
        for (const char *k : keys)
            if (!jsonField(obj, k).isEmpty()) return true;
        return false;
    };
    if (!hasAnyExif()) return;

    if (!camera.isEmpty()) addRow("Camera", camera);
    pushIf("Lens",     "LensModel");
    {
        const QString exp = jsonField(obj, "ExposureTime");
        if (!exp.isEmpty()) {
            // exiftool with -n returns ExposureTime as a decimal; format as 1/x
            bool ok = false;
            const double v = exp.toDouble(&ok);
            if (ok && v > 0 && v < 1.0)
                addRow("Exposure", QString("1/%1 s").arg(qRound(1.0 / v)));
            else
                addRow("Exposure", exp + " s");
        }
    }
    {
        const QString f = jsonField(obj, "FNumber");
        if (!f.isEmpty()) addRow("Aperture", "f/" + f);
    }
    pushIf("ISO",      "ISO");
    {
        const QString fl = jsonField(obj, "FocalLength");
        if (!fl.isEmpty()) addRow("Focal length", fl + " mm");
    }
    pushIf("Taken",    "DateTimeOriginal");
    {
        const QString lat = jsonField(obj, "GPSLatitude");
        const QString lon = jsonField(obj, "GPSLongitude");
        if (!lat.isEmpty() && !lon.isEmpty()) addRow("GPS", lat + ", " + lon);
    }
}
