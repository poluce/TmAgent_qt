#ifndef MESSAGEROUTER_H
#define MESSAGEROUTER_H

#include "core/model/Session.h"
#include <QHash>
#include <QString>
#include <QStringList>

/**
 * @brief MessageRouter 负责群聊/私聊的目标 Agent 路由决策
 *
 * 当前为可独立测试的纯计算组件，不依赖 UI 与 ChatService。
 */
class MessageRouter {
public:
    struct RouteInput {
        Session::SessionType sessionType = Session::SessionType::Private;
        QString senderIdentityId;
        QString userIdentityId;
        QString text;
        QStringList mentions; // 支持 "@name" / "name" / "agent-id"
        QStringList participantAgentIds;
        QHash<QString, QString> agentDisplayNames; // agentId -> display name
        bool defaultBroadcastFromUser = true;
    };

    struct RouteResult {
        QStringList targetAgentIds;
        QStringList mentionTokens;
        QStringList unresolvedMentions;
        bool isBroadcast = false;
        bool usedDefaultRoute = false;
    };

    static RouteResult route(const RouteInput& input);
    static QStringList parseMentions(const QString& text);

private:
    static QString normalizeMentionToken(const QString& token);
    static bool isBroadcastToken(const QString& token);
};

#endif // MESSAGEROUTER_H
