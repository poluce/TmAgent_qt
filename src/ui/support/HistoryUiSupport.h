#ifndef HISTORYUISUPPORT_H
#define HISTORYUISUPPORT_H

#include "AppFacade.h"
#include "core/model/Message.h"
#include "ExecutionHistoryModel.h"
#include "chat_widget.h"
#include <QJsonArray>
#include <QList>
#include <QVector>
#include <functional>

class Identity;
class QComboBox;

namespace HistoryUiSupport {

struct SessionRestoreOptions {
    QString viewerIdentityId;
    QString userSenderId = QStringLiteral("user");
    QString userDisplayName = QStringLiteral("用户");
    QString userAvatarPath;
    QString selfDisplayName = QStringLiteral("Me");
    QString defaultAssistantDisplayName = QStringLiteral("Agent");
    bool userMessagesAreMine = true;
    bool filterHeartbeatMessages = false;
    bool requireCompleteFilePayload = false;
    int maxRestoreMessages = 0;
    std::function<Identity*(const QString&)> identityResolver;
};

struct RawHistoryRestoreOptions {
    QString fallbackAssistantSenderId = QStringLiteral("assistant");
    QString assistantDisplayName = QStringLiteral("Assistant");
    QString assistantAvatarPath;
    QString userSenderId = QStringLiteral("user");
    QString userDisplayName = QStringLiteral("用户");
    QString userAvatarPath;
    bool userMessagesAreMine = true;
};

struct ExecutionHistoryState {
    QVector<ExecutionHistory::Record> records;
    QVector<int> visibleIndexes;
};

QList<ChatWidget::HistoryMessage> buildSessionHistoryMessages(const QList<Message>& allMessages,
                                                              const SessionRestoreOptions& options);
QList<ChatWidget::HistoryMessage> buildRawHistoryMessages(const QJsonArray& history,
                                                          const RawHistoryRestoreOptions& options);

void populateFilterCombo(QComboBox* combo);
void populateRecentCombo(QComboBox* combo);
ExecutionHistory::FilterMode selectedFilterMode(const QComboBox* combo);
int selectedRecentLimit(const QComboBox* combo);
QVector<int> buildVisibleHistoryIndexes(const QVector<ExecutionHistory::Record>& records,
                                        const QComboBox* filterCombo,
                                        const QComboBox* recentCombo);
ExecutionHistoryState buildExecutionHistoryState(const QJsonArray& history,
                                                 const QComboBox* filterCombo,
                                                 const QComboBox* recentCombo);
QJsonArray runtimeIoHistoryForSession(const IConversationService* viewQueries, const QString& sessionId);
void clearConversationHistory(IConversationService* viewCommands, const QString& sessionId);

} // namespace HistoryUiSupport

#endif // HISTORYUISUPPORT_H

