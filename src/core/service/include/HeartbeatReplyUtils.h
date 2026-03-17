#ifndef HEARTBEATREPLYUTILS_H
#define HEARTBEATREPLYUTILS_H

#include <QDateTime>
#include <QString>

namespace HeartbeatReplyUtils {

struct DeliveryDecision {
    bool suppress = false;
    QString reason;
    QString normalizedText;
    QString digest;
};

QString normalizeReplyText(const QString& text);
QString replyDigest(const QString& text);
DeliveryDecision evaluateReplyDelivery(
    const QString& text,
    bool backgroundHeartbeat,
    int duplicateWindowMs,
    const QDateTime& lastDeliveredAtUtc,
    const QString& lastDeliveredDigest,
    const QDateTime& nowUtc);

} // namespace HeartbeatReplyUtils

#endif // HEARTBEATREPLYUTILS_H
