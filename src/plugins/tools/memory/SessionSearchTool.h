#ifndef SESSIONSEARCHTOOL_H
#define SESSIONSEARCHTOOL_H

#include <tmagent/types/ToolTypes.h>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

class SessionSearchTool {
public:
    static TmAgent::Tool toolSchema();
    static QString executeSearch(const QJsonObject& args);

private:
    struct Hit {
        QString sessionId;
        QString messageId;
        QString senderId;
        QString turnId;
        QString traceId;
        QString snippet;
        QDateTime timestamp;
        qint64 timestampMs = -1;
        int seqNo = 0;
    };

    static QString dataRootPath();
    static QString normalizeScope(const QJsonObject& args);
    static QString resolveAgentId(const QJsonObject& args);
    static bool resolveTargetSessionsFromDb(const QString& scope, const QString& agentId, const QString& targetSessionId, QStringList* targetSessions, QString* error);
    static bool resolveTargetSessions(const QString& sessionsDataDir, const QString& scope, const QString& agentId, const QString& targetSessionId, QStringList* targetSessions, QString* error);
    static QVector<Hit> scanSessionMessagesFromDb(const QString& sessionId, const QString& query, bool includeToolMessages, int maxSnippetChars);
    static bool sessionContainsAgent(const QString& sessionsDataDir, const QString& sessionId, const QString& agentId);
    static QVector<Hit> scanSessionMessages(const QString& sessionId, const QString& messagesPath, const QString& query, bool includeToolMessages, int maxSnippetChars);
    static QString clipSnippet(const QString& text, int maxChars);
};

#endif // SESSIONSEARCHTOOL_H
