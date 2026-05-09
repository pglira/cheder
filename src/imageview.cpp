#include "imageview.h"

#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

ImageView::ImageView(QWidget *parent) : QWidget(parent) {
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet("background-color: black;");
    m_label->setMinimumSize(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_label);

    setFocusPolicy(Qt::StrongFocus);
}

void ImageView::setFiles(const QStringList &files) {
    m_files = files;
    if (m_index >= m_files.size()) m_index = m_files.isEmpty() ? 0 : m_files.size() - 1;
    if (m_index < 0) m_index = 0;
    loadCurrent();
}

void ImageView::setIndex(int index) {
    if (m_files.isEmpty()) return;
    if (index < 0) index = 0;
    if (index >= m_files.size()) index = m_files.size() - 1;
    m_index = index;
    loadCurrent();
}

QString ImageView::currentPath() const {
    if (m_index < 0 || m_index >= m_files.size()) return QString();
    return m_files.at(m_index);
}

void ImageView::next() {
    if (m_files.isEmpty()) return;
    m_index = (m_index + 1) % m_files.size();
    loadCurrent();
}

void ImageView::previous() {
    if (m_files.isEmpty()) return;
    m_index = (m_index - 1 + m_files.size()) % m_files.size();
    loadCurrent();
}

void ImageView::loadCurrent() {
    if (m_files.isEmpty()) {
        m_original = QPixmap();
        m_label->clear();
        return;
    }
    QImageReader reader(m_files.at(m_index));
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    m_original = img.isNull() ? QPixmap() : QPixmap::fromImage(img);
    updatePixmap();
    emit currentChanged(m_index, m_files.at(m_index));
}

void ImageView::updatePixmap() {
    if (m_original.isNull()) {
        m_label->setText("Failed to load image");
        m_label->setStyleSheet("background-color: black; color: #888;");
        return;
    }
    m_label->setStyleSheet("background-color: black;");
    m_label->setPixmap(m_original.scaled(m_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ImageView::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updatePixmap();
}
