#include "HeartbeatReplyUtils.h"

#include <QCryptographicHash>

namespace {

bool isNoChangeReply(const QString& text)
{
    const QString t = text.trimmed();
    return t == QStringLiteral("当前无关键更新。")
        || t == QStringLiteral("当前无关键更新")
        || t == QStringLiteral("无关键更新。")
        || t == QStringLiteral("无关键更新");
}

}

namespace HeartbeatReplyUtils {

QString normalizeReplyText(const QString& text)
{
    return text.simplified();
}

QString replyDigest(const QString& text)
{
    const QString normalized = normalizeReplyText(text);
    if (normalized.isEmpty())
        return QString();
    return QString::fromLatin1(
        QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex());
}

DeliveryDecision evaluateReplyDelivery(
    const QString& text,
    bool backgroundHeartbeat,
    int duplicateWindowMs,
    const QDateTime& lastDeliveredAtUtc,
    const QString& lastDeliveredDigest,
    const QDateTime& nowUtc)
{
    DeliveryDecision decision;
    decision.normalizedText = normalizeReplyText(text);
    if (decision.normalizedText.isEmpty()) {
        decision.suppress = true;
        decision.reason = QStringLiteral("empty_reply");
        return decision;
    }

    decision.digest = replyDigest(decision.normalizedText);
    if (!backgroundHeartbeat)
        return decision;

    if (isNoChangeReply(decision.normalizedText)) {
        decision.suppress = true;
        decision.reason = QStringLiteral("no_change_reply");
        return decision;
    }

    const int safeWindowMs = qMax(1000, duplicateWindowMs);
    const bool duplicateWithinWindow = !decision.digest.isEmpty()
        && !lastDeliveredDigest.isEmpty()
        && decision.digest == lastDeliveredDigest
        && lastDeliveredAtUtc.isValid()
        && lastDeliveredAtUtc.msecsTo(nowUtc) >= 0
        && lastDeliveredAtUtc.msecsTo(nowUtc) <= safeWindowMs;
    if (duplicateWithinWindow) {
        decision.suppress = true;
        decision.reason = QStringLiteral("duplicate_suppressed");
    }
    return decision;
}

} // namespace HeartbeatReplyUtils
