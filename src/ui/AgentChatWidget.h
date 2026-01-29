#ifndef AGENTCHATWIDGET_H
#define AGENTCHATWIDGET_H

#include "core/agent/LLMAgent.h"
#include <QColor>
#include <QHash>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

class ToolDispatcher;   // 前向声明
class ToolLogWidget;    // 前向声明
class ChatWidget;       // 前向声明
class ChatListWidget;   // 前向声明
class ModelFactory;     // 前向声明
class McpToolProvider;  // 前向声明

class AgentChatWidget : public QWidget {
    Q_OBJECT
public:
    explicit AgentChatWidget(QWidget* parent = nullptr);

private slots:
    void onModelConfigImportClicked();
    void onMcpConfigClicked();
    void onNewChatRequested();
    void onChatItemActivated(const QString &name, const QString &message, const QString &time,
                             const QColor &avatarColor, int unreadCount);
    void onChatItemRemoved(int row);
    void onUserMessageSent(const QString& content);
    void onAbortClicked();
    void onFinished(const QString& content);
    void onStreamDataReceived(const QString& data);
    void onErrorOccurred(const QString& errorMsg);
    void updateHistoryDisplay();
    void onClearHistoryClicked();
    void onRemoveCurrentChatRequested();

    // 工具事件处理（统一处理 started/completed）
    void onToolEvent(const ToolExecutionEvent& event);
    void onToolCallsStarted();

    // ChatWidget 输入子组件信号（语音等）
    void onVoiceStartRequested();
    void onVoiceStopRequested();

private:
    void setupUI();
    void loadConfig();
    void handleChatRowRemoved(int row, bool listAlreadyRemoved);
    void updateSendingStateForCurrentRow();
    bool isRowStreaming(int row) const;
    struct StreamState {
        QString buffer;
        bool hasPendingMessage = false;
        bool lastMsgIsTool = false;
        bool isStreaming = false;
    };
    StreamState &streamStateForRow(int row);
    void applyMcpConfig(const QStringList& specs);
    QStringList loadMcpConfigSpecs() const;
    bool saveMcpConfigSpecs(const QStringList& specs) const;
    QString mcpConfigPath() const;
    void setSendingState(bool isSending); // 设置发送状态
    void restoreChatFromHistory(const QJsonArray& history); // 从历史恢复聊天显示
    void clearChatMessages(); // 清空聊天区
    QString sessionsFilePath() const;     // 持久化文件路径
    void saveSessionsToDisk();            // 将会话列表与历史写入本地
    bool loadSessionsFromDisk();          // 从本地加载会话，成功返回 true
    void updateHistoryDisplayFrom(const QJsonArray& history); // 按给定历史刷新右侧历史面板
    LLMAgent* agentForRow(int row) const;
    LLMAgent* ensureAgentForRow(int row);
    void connectAgentSignals(LLMAgent* agent);
    void setCurrentAgentForRow(int row);
    void applyConfigToAllAgents();
    void applyToolDispatcherToAllAgents();
    QJsonArray historyForRow(int row) const;
    QJsonArray ioHistoryForRow(int row) const;
    void removeAgentForRow(int row);
    void reindexAgentsAfterRemoval(int removedRow);

    // 对话历史显示
    QTreeWidget* m_historyDisplay;
    QPushButton* m_clearHistoryBtn;
    QLabel* m_historyLabel;

    ModelFactory* m_modelFactory = nullptr;
    LLMAgent* m_currentAgent = nullptr;
    QHash<int, LLMAgent*> m_sessionAgents;
    LLMConfig m_defaultAgentConfig;
    ToolDispatcher* m_toolDispatcher;
    McpToolProvider* m_mcpProvider = nullptr;
    ToolLogWidget* m_toolLogWindow = nullptr; // 工具日志独立窗口

    class ChatWidget* m_chatWidget = nullptr;
    class ChatListWidget* m_chatListWidget = nullptr;
    QHash<int, QJsonArray> m_sessionHistories; // 源模型行号 -> 会话历史（多会话内存缓存）
    QHash<int, QJsonArray> m_sessionIoHistories; // 源模型行号 -> 会话 IO 历史
    int m_currentSessionRow = 0;                // 当前选中的会话在列表中的源行号
    QHash<int, StreamState> m_streamStates;

    // UI 显示模式（由 UI 自行管理，与 Agent 无关）
    bool m_isDebugMode = false;
};

#endif // AGENTCHATWIDGET_H
