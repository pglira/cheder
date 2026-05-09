#pragma once

#include <QPixmap>
#include <QStringList>
#include <QWidget>

class QLabel;

class ImageView : public QWidget {
    Q_OBJECT
public:
    explicit ImageView(QWidget *parent = nullptr);

    void setFiles(const QStringList &files);
    void setIndex(int index);
    int index() const { return m_index; }
    QString currentPath() const;

    void next();
    void previous();

signals:
    void currentChanged(int index, const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void loadCurrent();
    void updatePixmap();

    QLabel *m_label;
    QStringList m_files;
    int m_index = 0;
    QPixmap m_original;
};
