#pragma once

#include <QWidget>

class Action;
class ActionBar;
class ActionLogger;
class ActionRegistry;
class QLabel;
class QTextEdit;
class QToolButton;

// Bottom pane bundling the action input bar with a one-line log strip and
// an expandable history popup. Resting height is just the input row; the
// strip appears only after the first log entry, and the history popup is
// opt-in via the chevron toggle. Replaces the floating LogDock.
class ActionPane : public QWidget {
    Q_OBJECT
public:
    ActionPane(ActionRegistry *registry, QWidget *parent = nullptr);

    ActionLogger *logger() const { return m_logger; }

    // Forwarded from the embedded ActionBar so MainWindow can drive it
    // without reaching through to the bar directly.
    void focusInput();
    void resetState();
    bool isInputFocused() const;

signals:
    void actionInvoked(Action *action);
    void exitRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void appendLog(int level, const QString &message);
    void updateStripText();
    void toggleHistory();

    ActionLogger *m_logger;
    ActionBar    *m_bar;
    QWidget      *m_logStrip;
    QLabel       *m_logLatest;
    QToolButton  *m_logToggle;
    QTextEdit    *m_logHistory;

    QString m_lastFull;     // full "ts  message" string for elision
    int     m_lastLevel = 0;
};
