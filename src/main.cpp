#include "imagedir.h"
#include "mainwindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>

namespace {

bool promptForFileOrDir(QString *outFilePath, QString *outDirPath) {
    QMessageBox box;
    box.setWindowTitle("cheder");
    box.setText("Open an image file or a directory of images?");
    QPushButton *fileBtn = box.addButton("File...", QMessageBox::AcceptRole);
    QPushButton *dirBtn  = box.addButton("Directory...", QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == fileBtn) {
        const QString filter = "Images (" + supportedImageGlobs().join(' ') + ")";
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
    // Ties the window to the installed cheder.desktop entry so Wayland shells
    // show its icon and group its windows.
    app.setDesktopFileName("cheder");
    // Window icon (X11 _NET_WM_ICON / task-switcher / title bar). The sizes are
    // baked into the binary so the icon shows without the app being installed;
    // the .desktop file plus the hicolor theme cover the launcher separately.
    QIcon appIcon;
    for (int sz : {16, 24, 32, 48, 64, 128, 256})
        appIcon.addFile(QStringLiteral(":/icons/cheder-%1.png").arg(sz));
    app.setWindowIcon(appIcon);

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
