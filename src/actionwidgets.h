#pragma once

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

// Composes `edit` and a "..." button that opens a directory picker into a
// single horizontal row. The picker prefills with the edit's current text and
// writes the chosen path back into `edit` on accept. Returns the row widget,
// suitable for QFormLayout::addRow as the field widget.
//
// Header-only because every action dialog wires the same idiom — keeps the
// helper next to the action sources without a separate translation unit.
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
