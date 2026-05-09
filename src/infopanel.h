#pragma once

#include <QString>
#include <QWidget>

class ThumbnailCache;
class QFormLayout;
class QLabel;
class QProcess;
class QResizeEvent;

// Side panel that previews the selected image and lists its metadata.
// Basics (filename, dimensions, size, mtime, format) are populated
// synchronously; EXIF fields are filled asynchronously via `exiftool` when
// it is installed — otherwise the EXIF rows are simply omitted.
class InfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit InfoPanel(ThumbnailCache *cache, QWidget *parent = nullptr);

    // Empty path renders "No selection".
    void showFile(const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void clearForm();
    void addRow(const QString &label, const QString &value);
    void populateBasics(const QString &path);
    void requestExif(const QString &path);
    void onExifFinished();
    void refreshThumbnail();

    ThumbnailCache *m_cache;
    QLabel *m_thumb;
    QFormLayout *m_form;
    QProcess *m_exifProc = nullptr;
    QString m_currentPath;
};
