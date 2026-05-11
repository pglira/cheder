#include "imageview.h"

#include "filelistmodel.h"
#include "imageio.h"

#include <QImage>
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

ImageView::ImageView(FileListModel *model, QWidget *parent)
    : QWidget(parent), m_model(model) {
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet("background-color: black;");
    m_label->setMinimumSize(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_label);

    setFocusPolicy(Qt::StrongFocus);

    connect(m_model, &FileListModel::filesChanged, this, &ImageView::onFilesChanged);
    onFilesChanged();
}

void ImageView::onFilesChanged() {
    // Identity-first: if the previously-shown image is still in the list,
    // follow it to its (possibly shifted) new index. Only fall back to
    // clamping the old index when the path is gone (e.g. it was deleted or
    // moved). Without this, reload() loads the wrong image once via the
    // signal and again via the explicit setIndex() that callers use to
    // restore position.
    if (!m_currentPath.isEmpty()) {
        const int newIdx = m_model->indexOf(m_currentPath);
        if (newIdx >= 0) {
            m_index = newIdx;
            loadCurrent();
            return;
        }
    }
    const int n = m_model->count();
    if (m_index >= n) m_index = n == 0 ? 0 : n - 1;
    if (m_index < 0)  m_index = 0;
    loadCurrent();
}

void ImageView::setIndex(int index) {
    if (m_model->isEmpty()) return;
    if (index < 0) index = 0;
    if (index >= m_model->count()) index = m_model->count() - 1;
    m_index = index;
    loadCurrent();
}

void ImageView::next() {
    if (m_model->isEmpty()) return;
    m_index = (m_index + 1) % m_model->count();
    loadCurrent();
}

void ImageView::previous() {
    if (m_model->isEmpty()) return;
    m_index = (m_index - 1 + m_model->count()) % m_model->count();
    loadCurrent();
}

void ImageView::loadCurrent() {
    m_currentPath = m_model->at(m_index);
    if (m_currentPath.isEmpty()) {
        m_original = QPixmap();
        m_label->clear();
        return;
    }
    const QImage img = readImage(m_currentPath);
    m_original = img.isNull() ? QPixmap() : QPixmap::fromImage(img);
    updatePixmap();
    emit currentChanged(m_index, m_currentPath);
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
