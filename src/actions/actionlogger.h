#pragma once

#include <QObject>
#include <QString>

// Lightweight log channel actions write to during apply(). The MainWindow
// owns one and forwards entries to a dock pane. Passing nullptr is safe;
// log() becomes a no-op so actions don't need to null-check.
//
// While an action is running, every log entry is auto-prefixed with
// `[ActionName] ` so the pane reads like a clean per-action transcript.
// The prefix is installed by beginRun() and cleared by endRun(); WriteTarget
// helpers and any other code invoked from apply() get the prefix for free.
class ActionLogger : public QObject {
    Q_OBJECT
public:
    enum Level { Info, Warn, Error };
    Q_ENUM(Level)

    explicit ActionLogger(QObject *parent = nullptr) : QObject(parent) {}

    void info (const QString &message) { log(Info,  message); }
    void warn (const QString &message) { log(Warn,  message); }
    void error(const QString &message) { log(Error, message); }

    // Announces "[<action>] start — <count> input(s)" so each run starts
    // with a clear separator in the pane, and stashes the action name so
    // subsequent info/warn/error calls inside apply() get the same prefix.
    void beginRun(const QString &actionName, int inputCount) {
        m_currentAction = actionName;
        info(QString("start — %1 input(s)").arg(inputCount));
    }
    void endRun(int writtenCount, int skippedCount, int failedCount) {
        info(QString("done — wrote %1, skipped %2, failed %3")
                 .arg(writtenCount).arg(skippedCount).arg(failedCount));
        m_currentAction.clear();
    }

signals:
    void logged(int level, const QString &message);

private:
    void log(Level level, const QString &message) {
        const QString out = m_currentAction.isEmpty()
            ? message
            : QStringLiteral("[%1] %2").arg(m_currentAction, message);
        emit logged(static_cast<int>(level), out);
    }

    QString m_currentAction;
};
