#include "actionbar.h"

#include "action.h"
#include "actionregistry.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

ActionBar::ActionBar(ActionRegistry *registry, QWidget *parent)
    : QWidget(parent), m_registry(registry) {
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    m_list->setMaximumHeight(180);
    m_list->setVisible(false);
    m_list->setFocusPolicy(Qt::NoFocus);  // keep keyboard focus on the input

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText("Filter actions — Enter runs, Esc/Tab dismisses (Ctrl+P to focus)");
    m_input->installEventFilter(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    layout->addWidget(m_list);
    layout->addWidget(m_input);

    connect(m_input, &QLineEdit::textChanged, this, &ActionBar::onTextChanged);
    connect(m_list, &QListWidget::itemActivated, this, [this] { invokeCurrent(); });
}

void ActionBar::focusInput() {
    m_input->setFocus();
    m_input->selectAll();
}

void ActionBar::resetState() {
    m_input->clear();
    m_list->setVisible(false);  // explicit: input may still hold focus here
}

bool ActionBar::isInputFocused() const {
    return m_input->hasFocus();
}

void ActionBar::onTextChanged(const QString &text) {
    rebuildList(text);
    // Only show while the input has focus; clearing the field after running
    // an action would otherwise repopulate the list mid-dismissal.
    m_list->setVisible(m_input->hasFocus() && m_list->count() > 0);
}

void ActionBar::rebuildList(const QString &filter) {
    m_list->clear();
    const QString needle = filter.trimmed().toLower();
    for (Action *a : m_registry->all()) {
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

void ActionBar::invokeCurrent() {
    auto *item = m_list->currentItem();
    if (!item) return;
    auto *action = static_cast<Action *>(item->data(Qt::UserRole).value<void *>());
    if (action) emit actionInvoked(action);
}

bool ActionBar::eventFilter(QObject *obj, QEvent *event) {
    if (obj != m_input) return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::FocusIn) {
        rebuildList(m_input->text());
        m_list->setVisible(m_list->count() > 0);
        return false;
    }
    if (event->type() == QEvent::FocusOut) {
        m_list->setVisible(false);
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(obj, event);

    auto *ke = static_cast<QKeyEvent *>(event);
    switch (ke->key()) {
    case Qt::Key_Down:
        if (m_list->count() > 0)
            m_list->setCurrentRow(std::min(m_list->currentRow() + 1, m_list->count() - 1));
        return true;
    case Qt::Key_Up:
        if (m_list->count() > 0)
            m_list->setCurrentRow(std::max(m_list->currentRow() - 1, 0));
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        invokeCurrent();
        return true;
    case Qt::Key_Escape:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        resetState();
        emit exitRequested();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}
