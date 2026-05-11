#include "metadatareader.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

MetadataReader::MetadataReader(QObject *parent)
    : QObject(parent), m_proc(new QProcess(this)) {
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) { onFinished(); });
}

void MetadataReader::request(const QString &path) {
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(50);
    }
    m_pendingPath = path;
    m_proc->start("exiftool", {"-j", "-n", "--", path});
}

void MetadataReader::onFinished() {
    if (m_proc->exitStatus() != QProcess::NormalExit || m_proc->exitCode() != 0) return;
    const QByteArray out = m_proc->readAllStandardOutput();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray() || doc.array().isEmpty()) return;
    emit metadataReady(m_pendingPath, doc.array().first().toObject());
}
