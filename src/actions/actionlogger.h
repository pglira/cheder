#pragma once

#include <QObject>
#include <QString>

// Lightweight log channel actions write to during apply(). The MainWindow
// owns one and forwards entries to a dock pane. Passing nullptr is safe;
// log() becomes a no-op so actions don't need to null-check.
class ActionLogger : public QObject {
    Q_OBJECT
public:
    enum Level { Info, Warn, Error };
    Q_ENUM(Level)

    explicit ActionLogger(QObject *parent = nullptr) : QObject(parent) {}

    void info (const QString &message) { log(Info,  message); }
    void warn (const QString &message) { log(Warn,  message); }
    void error(const QString &message) { log(Error, message); }

    // Convenience for actions: announces "<action>: <count> input(s)" so each
    // run starts with a clear separator in the pane.
    void beginRun(const QString &actionName, int inputCount) {
        info(QString("[%1] start — %2 input(s)").arg(actionName).arg(inputCount));
    }
    void endRun(const QString &actionName, int writtenCount, int skippedCount, int failedCount) {
        info(QString("[%1] done — wrote %2, skipped %3, failed %4")
                 .arg(actionName).arg(writtenCount).arg(skippedCount).arg(failedCount));
    }

signals:
    void logged(int level, const QString &message);

private:
    void log(Level level, const QString &message) {
        emit logged(static_cast<int>(level), message);
    }
};
