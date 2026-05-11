#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// Single source of truth for the list of image paths the app is currently
// working with. Owned by MainWindow; ImageView and ThumbnailView observe it.
// Replaces the previous arrangement where each view kept its own copy and
// MainWindow kept them in sync via setFiles() calls.
class FileListModel : public QObject {
    Q_OBJECT
public:
    explicit FileListModel(const QStringList &files = {}, QObject *parent = nullptr)
        : QObject(parent), m_files(files) {}

    const QStringList &files() const { return m_files; }
    int     count()   const { return m_files.size(); }
    bool    isEmpty() const { return m_files.isEmpty(); }
    QString at(int i) const { return (i >= 0 && i < m_files.size()) ? m_files.at(i) : QString(); }
    int     indexOf(const QString &path) const { return m_files.indexOf(path); }

    void setFiles(const QStringList &files) {
        if (m_files == files) return;
        m_files = files;
        emit filesChanged();
    }

signals:
    void filesChanged();

private:
    QStringList m_files;
};
