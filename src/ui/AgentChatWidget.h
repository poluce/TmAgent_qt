#ifndef AGENTCHATWIDGET_H
#define AGENTCHATWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QCheckBox>
#include "core/agent/LLMAgent.h"

class ToolDispatcher;  // 前向声明
class ToolLogWidget;   // 前向声明
class QChatWidget;     // 前向声明
class QTextBrowser;    // 前向声明

class AgentChatWidget : public QWidget {
    Q_OBJECT
public:
    explicit AgentChatWidget(QWidget *parent = nullptr);

private slots:
    void onSaveClicked();
    void onUserMessageSent(const QString& content);
    void onAbortClicked();
    void onFinished(const QString& content);
    void onStreamDataReceived(const QString& data);
    void onErrorOccurred(const QString& errorMsg);
    void updateHistoryDisplay();
    void onClearHistoryClicked();
    void onTestToolClicked();
    
    // 工具事件处理（统一处理 started/completed）
    void onToolEvent(const ToolExecutionEvent& event);
    void onToolCallsStarted();

private:
    void setupUI();
    void loadConfig();
    void setSendingState(bool isSending);             // 设置发送状态

    // UI Widgets
    QLineEdit *m_baseUrlEdit;
    QLineEdit *m_apiKeyEdit;
    QLineEdit *m_modelEdit;
    QTextEdit *m_systemPromptEdit;
    
    QPushButton *m_saveBtn;
    QPushButton *m_abortBtn;
    
    // 对话历史显示
    QTextBrowser *m_historyDisplay;
    QPushButton *m_clearHistoryBtn;
    QLabel *m_historyLabel;
    
    // 工具测试
    QPushButton *m_testToolBtn;
    
    // 阶段三: 调试模式复选框
    QCheckBox *m_debugModeCheck;

    LLMAgent *m_agent;
    ToolDispatcher *m_toolDispatcher;
    ToolLogWidget *m_toolLogWindow = nullptr; // 工具日志独立窗口
    
    class QChatWidget *m_chatWidget = nullptr;
    QString m_currentAssistantReply;  // 当前助手回复的累积内容
    bool m_hasPendingAssistantMessage = false;
    
    // UI 显示模式（由 UI 自行管理，与 Agent 无关）
    bool m_isDebugMode = false;
};

#endif // AGENTCHATWIDGET_H

