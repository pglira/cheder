#pragma once

#include <QString>
#include <QWidget>

class QFormLayout;
class QProcess;

// Side panel listing the selected image's metadata. Basics (filename,
// dimensions, size, mtime, format) are populated synchronously; EXIF fields
// are filled asynchronously via `exiftool` when it is installed — otherwise
// the EXIF rows are simply omitted.
class InfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit InfoPanel(QWidget *parent = nullptr);

    // Empty path renders "No selection".
    void showFile(const QString &path);

private:
    void clearForm();
    void addRow(const QString &label, const QString &value);
    void populateBasics(const QString &path);
    void requestExif(const QString &path);
    void onExifFinished();

    QFormLayout *m_form;
    QProcess *m_exifProc = nullptr;
    QString m_currentPath;
};
