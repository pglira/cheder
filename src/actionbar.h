#pragma once

#include <QWidget>

class Action;
class ActionRegistry;
class QLineEdit;
class QListWidget;

// Always-visible bar with a search field that filters actions and shows
// matches in a list above it. The list is shown only when the filter is
// non-empty and produces results, so the bar collapses to a single-row
// input when idle.
class ActionBar : public QWidget {
    Q_OBJECT
public:
    explicit ActionBar(ActionRegistry *registry, QWidget *parent = nullptr);

    void focusInput();              // give keyboard focus to the search field
    void resetState();              // clear input and hide result list
    bool isInputFocused() const;    // for upstream key-translation guards

signals:
    void actionInvoked(Action *action);
    void exitRequested();           // Esc/Tab while focused — caller restores focus

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void onTextChanged(const QString &text);
    void invokeCurrent();
    void rebuildList(const QString &filter);

    ActionRegistry *m_registry;
    QLineEdit *m_input;
    QListWidget *m_list;
};
