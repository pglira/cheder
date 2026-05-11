#pragma once

#include <QString>
#include <QWidget>

class QFormLayout;
class QProcess;
class QShowEvent;

// Side panel listing the selected image's metadata. Basics (filename,
// dimensions, size, mtime, format) are populated synchronously; EXIF fields
// are filled asynchronously via `exiftool` when it is installed — otherwise
// the EXIF rows are simply omitted.
//
// Visibility self-managed: callers tell the panel which image is current,
// and the panel refreshes — including spawning exiftool — only while it is
// visible. Toggling the panel from hidden→visible triggers a refresh from
// the stored path. Callers don't gate on visibility themselves.
class InfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit InfoPanel(QWidget *parent = nullptr);

    // Empty path renders "No selection" on next refresh.
    void setCurrentPath(const QString &path);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void refresh();
    void clearForm();
    void addRow(const QString &label, const QString &value);
    void populateBasics(const QString &path);
    void requestExif(const QString &path);
    void onExifFinished();

    QFormLayout *m_form;
    QProcess *m_exifProc = nullptr;
    QString m_currentPath;
};
