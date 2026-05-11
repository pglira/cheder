#include "action.h"

#include "actionlogger.h"

#include <QDir>
#include <QFileInfo>

QStringList BatchAction::apply(const QStringList &inputs, ActionLogger *logger) {
    if (logger) logger->beginRun(name(), inputs.size());

    m_skippedThisRun = 0;
    m_failedThisRun  = 0;

    QStringList outputs;
    outputs.reserve(inputs.size());
    for (const QString &in : inputs) {
        const QString out = applyOne(in, logger);
        if (!out.isEmpty()) outputs << out;
    }

    // Anything we lost that wasn't accounted for as skip is a failure.
    const int written = outputs.size();
    int failed  = m_failedThisRun;
    int skipped = m_skippedThisRun;
    const int unaccounted = inputs.size() - written - failed - skipped;
    if (unaccounted > 0) failed += unaccounted;

    if (logger) logger->endRun(name(), written, skipped, failed);
    return outputs;
}

QString BatchAction::resolveOutputPath(const QString &input, ActionLogger *logger) {
    const auto r = WriteTarget::resolve(m_outDir, QFileInfo(input).fileName(),
                                        m_overwrite, logger, input);
    if (r.status == WriteTarget::ResolveStatus::Skip)   ++m_skippedThisRun;
    if (r.status == WriteTarget::ResolveStatus::Failed) ++m_failedThisRun;
    return r.path;
}

QString BatchAction::writeOne(const QString &input,
                              ActionLogger *logger,
                              std::function<bool(const QString &)> writer) {
    const QString finalPath = resolveOutputPath(input, logger);
    if (finalPath.isEmpty()) return {};

    QDir().mkpath(m_outDir);
    const QString out = WriteTarget::write(finalPath, logger, writer);
    if (out.isEmpty()) ++m_failedThisRun;
    return out;
}
