#pragma once

#include <QDialog>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

// Default width for all action parameter dialogs. Set as a minimum width so
// the dialog opens wide but the user can grow it further if they want.
constexpr int kActionDialogMinWidth = 800;

// Bold label summarising how many input files the action will run against.
inline QLabel *makeInputsLabel(int count, QWidget *parent) {
    auto *label = new QLabel(parent);
    label->setText(QString("Inputs: %1 file%2").arg(count).arg(count == 1 ? "" : "s"));
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

// Common dialog setup: enforce the standard width and let layout determine
// height. Call after the layout has been built.
inline void styleActionDialog(QDialog &dlg) {
    dlg.setMinimumWidth(kActionDialogMinWidth);
}

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
