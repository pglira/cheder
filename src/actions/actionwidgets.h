#pragma once

#include <QDialog>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

// Set as a minimum so the dialog opens wide but the user can grow it.
constexpr int kActionDialogMinWidth = 800;

inline QLabel *makeInputsLabel(int count, QWidget *parent) {
    auto *label = new QLabel(parent);
    label->setText(QString("Inputs: %1 file%2").arg(count).arg(count == 1 ? "" : "s"));
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

inline void styleActionDialog(QDialog &dlg) {
    dlg.setMinimumWidth(kActionDialogMinWidth);
}

// Wraps `edit` with a "..." button that opens a directory picker; the picker
// prefills with the edit's current text and writes the choice back. Returns
// the composed row, suitable as a QFormLayout field widget.
inline QWidget *outputDirField(QLineEdit *edit, QWidget *parent) {
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *browse = new QPushButton("...", row);
    browse->setMaximumWidth(40);
    browse->setToolTip("Choose output directory");
    layout->addWidget(edit);
    layout->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, parent, [edit, parent] {
        const QString picked = QFileDialog::getExistingDirectory(
            parent, "Choose output directory", edit->text());
        if (!picked.isEmpty()) edit->setText(picked);
    });
    return row;
}
