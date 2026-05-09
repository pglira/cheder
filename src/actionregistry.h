#pragma once

#include <QList>
#include <memory>

class Action;

class ActionRegistry {
public:
    ActionRegistry();
    ~ActionRegistry();

    void add(std::unique_ptr<Action> action);

    QList<Action *> all() const;
    QList<Action *> acceptingCount(int n) const;

private:
    std::vector<std::unique_ptr<Action>> m_actions;
};
