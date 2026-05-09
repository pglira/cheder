#pragma once

#include <QDialog>
#include <QList>

class Action;
class QLineEdit;
class QListWidget;

// Modal popup for choosing an Action by fuzzy-matching its name.
// Use exec() like any QDialog; on accept(), chosenAction() returns the pick.
class ActionPalette : public QDialog {
    Q_OBJECT
public:
    ActionPalette(const QList<Action *> &actions, QWidget *parent = nullptr);

    Action *chosenAction() const { return m_chosen; }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void rebuildList(const QString &filter);
    void acceptCurrent();

    QList<Action *> m_actions;
    QLineEdit *m_filter;
    QListWidget *m_list;
    Action *m_chosen = nullptr;
};
