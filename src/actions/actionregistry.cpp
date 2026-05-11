#include "actionregistry.h"

#include "action.h"

#include <algorithm>

ActionRegistry::ActionRegistry()  = default;
ActionRegistry::~ActionRegistry() = default;

void ActionRegistry::add(std::unique_ptr<Action> action) {
    if (!action) return;
    // Keep m_actions sorted by display name so all() and acceptingCount()
    // return actions in alphabetical order without per-call sorting.
    const QString name = action->name();
    const auto pos = std::upper_bound(
        m_actions.begin(), m_actions.end(), name,
        [](const QString &n, const std::unique_ptr<Action> &a) {
            return n.compare(a->name(), Qt::CaseInsensitive) < 0;
        });
    m_actions.insert(pos, std::move(action));
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
