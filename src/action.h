#pragma once

#include <QString>
#include <QStringList>

class QWidget;

// Base interface for an image-processing action that can be invoked from the
// action palette. Subclasses own their own parameter dialog and execution
// logic. Per-image actions should subclass BatchAction below to avoid
// reimplementing the per-input loop.
class Action {
public:
    virtual ~Action() = default;

    // Stable identifier; also used as the default output subdirectory name.
    virtual QString id() const = 0;

    // Human-readable label shown in the palette.
    virtual QString name() const = 0;

    // Optional secondary line shown in the palette.
    virtual QString description() const { return {}; }

    // Whether this action can run with `n` input images.
    virtual bool acceptsCount(int n) const = 0;

    // Show parameter dialog. Returns false if the user cancelled.
    // `inputs` is the list of source paths the action will be applied to —
    // actions render a count or summary so the user sees what they're about
    // to operate on. `defaultOutDir` is the suggested output directory
    // derived from the first input's location and this action's id().
    virtual bool configure(QWidget *parent,
                           const QStringList &inputs,
                           const QString &defaultOutDir) = 0;

    // Run the action on `inputs`. Returns the list of files actually written.
    virtual QStringList apply(const QStringList &inputs) = 0;
};

// Convenience base for actions that map one input -> one output.
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

    QString outputDir() const { return m_outDir; }

protected:
    // Run on one input, return produced output path (or empty on failure).
    virtual QString applyOne(const QString &input) = 0;

    QString m_outDir;
};
