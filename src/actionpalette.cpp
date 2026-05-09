#include "actionpalette.h"

#include "action.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

ActionPalette::ActionPalette(const QList<Action *> &actions, QWidget *parent)
    : QDialog(parent), m_actions(actions) {
    setWindowTitle("Actions");
    resize(480, 320);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText("Search actions...");
    m_filter->installEventFilter(this);

    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_filter);
    layout->addWidget(m_list);

    connect(m_filter, &QLineEdit::textChanged, this, &ActionPalette::rebuildList);
    connect(m_list, &QListWidget::itemActivated, this, [this] { acceptCurrent(); });

    rebuildList({});
    m_filter->setFocus();
}

void ActionPalette::rebuildList(const QString &filter) {
    m_list->clear();
    const QString needle = filter.trimmed().toLower();
    for (Action *a : m_actions) {
        if (!needle.isEmpty()) {
            const bool hit = a->name().toLower().contains(needle)
                          || a->id().toLower().contains(needle)
                          || a->description().toLower().contains(needle);
            if (!hit) continue;
        }
        const QString label = a->description().isEmpty()
                                  ? a->name()
                                  : a->name() + " — " + a->description();
        auto *item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, QVariant::fromValue(static_cast<void *>(a)));
    }
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

void ActionPalette::acceptCurrent() {
    auto *item = m_list->currentItem();
    if (!item) return;
    m_chosen = static_cast<Action *>(item->data(Qt::UserRole).value<void *>());
    accept();
}

bool ActionPalette::eventFilter(QObject *obj, QEvent *event) {
    // Forward up/down/enter from the line edit so the user never has to leave
    // the search box to pick an action.
    if (obj == m_filter && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Down: {
            const int n = m_list->count();
            if (n > 0) m_list->setCurrentRow(std::min(m_list->currentRow() + 1, n - 1));
            return true;
        }
        case Qt::Key_Up: {
            if (m_list->count() > 0) m_list->setCurrentRow(std::max(m_list->currentRow() - 1, 0));
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            acceptCurrent();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}
