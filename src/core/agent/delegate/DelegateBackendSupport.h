#ifndef DELEGATEBACKENDSUPPORT_H
#define DELEGATEBACKENDSUPPORT_H

#include <QString>

namespace DelegateBackendInternal {

QString buildExecutionPrompt(const QString& task);
QString normalizeDelegateBackend(const QString& rawBackend);
QString extractStatusTag(const QString& text);
bool isBlockedStatus(const QString& statusTag);
QString canonicalStatusTag(const QString& rawStatus);
QString ensureStructuredDelegateOutput(
    const QString& task,
    const QString& rawText,
    const QString& statusHint,
    bool* normalizedByScheduler = nullptr);

} // namespace DelegateBackendInternal

#endif // DELEGATEBACKENDSUPPORT_H
