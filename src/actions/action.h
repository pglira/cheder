#pragma once

#include <QString>
#include <QStringList>

class QWidget;

// Image-processing operation invoked from the action bar. Subclasses own
// their parameter dialog and execution. Per-input actions should subclass
// BatchAction to avoid reimplementing the loop.
class Action {
public:
    virtual ~Action() = default;

    // Stable identifier. Also used as the default output subdirectory name.
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString description() const { return {}; }

    virtual bool acceptsCount(int n) const = 0;

    // Show the parameter dialog. Returns false on cancel. `inputs` lets the
    // dialog summarise what it will operate on; `defaultOutDir` is the
    // suggested output (first input's parent dir + this action's id()).
    virtual bool configure(QWidget *parent,
                           const QStringList &inputs,
                           const QString &defaultOutDir) = 0;

    // Run on `inputs`; return the paths actually written.
    virtual QStringList apply(const QStringList &inputs) = 0;
};

// One input -> one output. Subclasses just implement applyOne().
class BatchAction : public Action {
public:
    bool acceptsCount(int n) const override { return n >= 1; }

    QStringList apply(const QStringList &inputs) override {
        QStringList outputs;
        outputs.reserve(inputs.size());
        for (const QString &in : inputs) {
            const QString out = applyOne(in);
            if (!out.isEmpty()) outputs << out;
        }
        return outputs;
    }

protected:
    // Return the produced output path, or {} on failure.
    virtual QString applyOne(const QString &input) = 0;

    QString m_outDir;
};
