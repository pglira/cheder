#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

// Async wrapper around `exiftool` that hands back parsed JSON metadata.
//
// Only one request is in flight at a time per reader — calling request()
// while a previous fetch is still running kills the old process so the
// caller's most recent ask wins. Each completed request emits
// metadataReady(path, json). Failures (exiftool missing, non-zero exit,
// malformed JSON) are silent — no signal is emitted.
//
// Stale-request handling (a fetch completing for a path the caller no
// longer cares about) is the caller's job: every emitted signal carries
// the path it was fetched for, so the caller can compare against its
// current selection.
class MetadataReader : public QObject {
    Q_OBJECT
public:
    explicit MetadataReader(QObject *parent = nullptr);

    void request(const QString &path);

signals:
    void metadataReady(const QString &path, const QJsonObject &metadata);

private:
    void onFinished();

    QProcess *m_proc;
    QString   m_pendingPath;
};
