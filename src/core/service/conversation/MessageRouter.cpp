#include "MessageRouter.h"

#include <QRegularExpression>
#include <QSet>

namespace {
QStringList dedupePreserveOrder(const QStringList& values)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString& v : values) {
        if (v.isEmpty() || seen.contains(v))
            continue;
        seen.insert(v);
        out.append(v);
    }
    return out;
}

QString resolveByIdCaseInsensitive(const QStringList& participantAgentIds, const QString& normalizedToken)
{
    for (const QString& agentId : participantAgentIds) {
        if (agentId.trimmed().toLower() == normalizedToken)
            return agentId;
    }
    return QString();
}
} // namespace

QStringList MessageRouter::parseMentions(const QString& text)
{
    QStringList tokens;
    if (text.trimmed().isEmpty())
        return tokens;

    static const QRegularExpression pattern(QStringLiteral("@([\\p{L}\\p{N}_\\-\\.]+)"));
    QRegularExpressionMatchIterator it = pattern.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString token = normalizeMentionToken(m.captured(1));
        if (!token.isEmpty())
            tokens.append(token);
    }
    return dedupePreserveOrder(tokens);
}

MessageRouter::RouteResult MessageRouter::route(const RouteInput& input)
{
    RouteResult result;

    const QStringList participantAgentIds = dedupePreserveOrder(input.participantAgentIds);
    if (participantAgentIds.isEmpty())
        return result;

    QStringList tokens;
    if (!input.mentions.isEmpty()) {
        for (const QString& mention : input.mentions) {
            const QString token = normalizeMentionToken(mention);
            if (!token.isEmpty())
                tokens.append(token);
        }
        tokens = dedupePreserveOrder(tokens);
    } else {
        tokens = parseMentions(input.text);
    }
    result.mentionTokens = tokens;

    QHash<QString, QString> normalizedNameToId;
    for (const QString& agentId : participantAgentIds) {
        const QString normalizedName = normalizeMentionToken(input.agentDisplayNames.value(agentId));
        if (!normalizedName.isEmpty() && !normalizedNameToId.contains(normalizedName))
            normalizedNameToId.insert(normalizedName, agentId);
    }

    const auto resolveMentionTarget = [&](const QString& token) -> QString {
        if (token.isEmpty())
            return QString();
        QString matchedId = resolveByIdCaseInsensitive(participantAgentIds, token);
        if (!matchedId.isEmpty())
            return matchedId;
        return normalizedNameToId.value(token);
    };

    const auto routeAll = [&]() {
        result.targetAgentIds = participantAgentIds;
        result.isBroadcast = true;
    };

    for (const QString& token : tokens) {
        if (isBroadcastToken(token)) {
            routeAll();
            result.unresolvedMentions.clear();
            return result;
        }
        const QString matchedId = resolveMentionTarget(token);
        if (matchedId.isEmpty()) {
            result.unresolvedMentions.append(token);
            continue;
        }
        if (!result.targetAgentIds.contains(matchedId))
            result.targetAgentIds.append(matchedId);
    }

    if (!result.targetAgentIds.isEmpty())
        return result;

    if (input.sessionType == Session::SessionType::Private) {
        routeAll();
        result.usedDefaultRoute = true;
        return result;
    }

    const bool noMentions = tokens.isEmpty();
    const bool senderIsUser = (!input.userIdentityId.trimmed().isEmpty()
                               && input.senderIdentityId.trimmed() == input.userIdentityId.trimmed());
    if (noMentions && input.defaultBroadcastFromUser && senderIsUser) {
        routeAll();
        result.usedDefaultRoute = true;
    }
    return result;
}

QString MessageRouter::normalizeMentionToken(const QString& token)
{
    QString out = token.trimmed();
    while (out.startsWith(QLatin1Char('@')))
        out.remove(0, 1);
    return out.trimmed().toLower();
}

bool MessageRouter::isBroadcastToken(const QString& token)
{
    const QString normalized = normalizeMentionToken(token);
    return normalized == QLatin1String("all")
        || normalized == QStringLiteral("所有")
        || normalized == QStringLiteral("全部")
        || normalized == QStringLiteral("everyone");
}
