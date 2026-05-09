#include "actionregistry.h"

#include "action.h"

ActionRegistry::ActionRegistry()  = default;
ActionRegistry::~ActionRegistry() = default;

void ActionRegistry::add(std::unique_ptr<Action> action) {
    if (action) m_actions.push_back(std::move(action));
}

QList<Action *> ActionRegistry::all() const {
    QList<Action *> out;
    out.reserve(static_cast<int>(m_actions.size()));
    for (const auto &a : m_actions) out << a.get();
    return out;
}

QList<Action *> ActionRegistry::acceptingCount(int n) const {
    QList<Action *> out;
    for (const auto &a : m_actions)
        if (a->acceptsCount(n)) out << a.get();
    return out;
}
