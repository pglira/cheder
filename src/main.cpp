#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>

namespace {

QStringList supportedExtensionGlobs() {
    QStringList globs;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        globs << "*." + QString::fromLatin1(fmt).toLower();
    return globs;
}

QStringList listImagesInDir(const QString &dir) {
    QDir d(dir);
    const QStringList names = d.entryList(supportedExtensionGlobs(),
                                          QDir::Files | QDir::Readable,
                                          QDir::Name | QDir::IgnoreCase);
    QStringList absolute;
    absolute.reserve(names.size());
    for (const QString &n : names) absolute << d.absoluteFilePath(n);
    return absolute;
}

bool promptForFileOrDir(QString *outFilePath, QString *outDirPath) {
    QMessageBox box;
    box.setWindowTitle("cheder");
    box.setText("Open an image file or a directory of images?");
    QPushButton *fileBtn = box.addButton("File...", QMessageBox::AcceptRole);
    QPushButton *dirBtn  = box.addButton("Directory...", QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == fileBtn) {
        const QString filter = "Images (" + supportedExtensionGlobs().join(' ') + ")";
        const QString sel = QFileDialog::getOpenFileName(nullptr, "Open image", QString(), filter);
        if (sel.isEmpty()) return false;
        *outFilePath = sel;
        return true;
    }
    if (box.clickedButton() == dirBtn) {
        const QString sel = QFileDialog::getExistingDirectory(nullptr, "Open directory");
        if (sel.isEmpty()) return false;
        *outDirPath = sel;
        return true;
    }
    return false;
}

struct StartupInput {
    QStringList files;
    int initialIndex = 0;
    bool startInImageView = false;
};

bool resolveStartupInput(const QString &filePath, const QString &dirPath, StartupInput *out) {
    if (!filePath.isEmpty()) {
        const QFileInfo fi(filePath);
        out->files = listImagesInDir(fi.absolutePath());
        out->initialIndex = out->files.indexOf(fi.absoluteFilePath());
        if (out->initialIndex < 0) {
            out->files.prepend(fi.absoluteFilePath());
            out->initialIndex = 0;
        }
        out->startInImageView = true;
        return true;
    }
    out->files = listImagesInDir(dirPath);
    if (out->files.isEmpty()) {
        QMessageBox::critical(nullptr, "cheder", "No images found in: " + dirPath);
        return false;
    }
    out->startInImageView = false;
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("cheder");

    QString filePath, dirPath;

    if (argc >= 2) {
        const QFileInfo fi(QString::fromLocal8Bit(argv[1]));
        if (!fi.exists()) {
            QMessageBox::critical(nullptr, "cheder", "Path does not exist: " + fi.filePath());
            return 1;
        }
        (fi.isDir() ? dirPath : filePath) = fi.absoluteFilePath();
    } else if (!promptForFileOrDir(&filePath, &dirPath)) {
        return 0;
    }

    StartupInput input;
    if (!resolveStartupInput(filePath, dirPath, &input)) return 1;

    MainWindow w(input.files);
    if (input.startInImageView) w.showImage(input.initialIndex);
    else                        w.showThumbnails();
    w.showMaximized();
    return app.exec();
}
