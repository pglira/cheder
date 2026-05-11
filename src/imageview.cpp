#include "imageview.h"

#include "filelistmodel.h"
#include "imageio.h"

#include <QImage>
#include <QLabel>
#include <QMovie>
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

ImageView::~ImageView() = default;

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

    // Tear down the previous animation (if any) before loading the next path
    // so a navigation away from a GIF stops its decode/play loop promptly.
    m_movie.reset();
    m_label->setMovie(nullptr);
    m_original = QPixmap();

    if (m_currentPath.isEmpty()) {
        m_label->clear();
        return;
    }

    if (isGifPath(m_currentPath)) {
        m_movie = std::make_unique<QMovie>(m_currentPath);
        m_movie->setCacheMode(QMovie::CacheAll);
        if (m_movie->isValid()) {
            m_label->setStyleSheet("background-color: black;");
            m_label->setMovie(m_movie.get());
            applyMovieScale();
            m_movie->start();
        } else {
            m_movie.reset();
            m_label->setText("Failed to load GIF");
            m_label->setStyleSheet("background-color: black; color: #888;");
        }
    } else {
        const QImage img = readImage(m_currentPath);
        m_original = img.isNull() ? QPixmap() : QPixmap::fromImage(img);
        rescale();
    }
    emit currentChanged(m_index, m_currentPath);
}

void ImageView::applyMovieScale() {
    // QMovie::frameRect() isn't populated until the first frame is jumped to;
    // peek the file's dimensions directly so scaling is correct from frame 1.
    const QSize native = peekImageSize(m_currentPath);
    if (!native.isValid()) return;
    QSize target = native;
    target.scale(m_label->size(), Qt::KeepAspectRatio);
    m_movie->setScaledSize(target);
}

void ImageView::rescale() {
    if (m_movie) { applyMovieScale(); return; }
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
    rescale();
}
