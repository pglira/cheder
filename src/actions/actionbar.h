#pragma once

#include <QWidget>

class Action;
class ActionRegistry;
class QLineEdit;
class QListWidget;

// Always-visible bar with a search field that filters actions. The result
// list above the input is shown only while the filter has matches, so the
// bar collapses to a single row when idle.
class ActionBar : public QWidget {
    Q_OBJECT
public:
    explicit ActionBar(ActionRegistry *registry, QWidget *parent = nullptr);

    void focusInput();
    void resetState();
    bool isInputFocused() const;

signals:
    void actionInvoked(Action *action);
    // Esc/Tab while focused — caller is expected to restore view focus.
    void exitRequested();

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
