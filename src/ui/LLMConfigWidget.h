#ifndef LLMCONFIGWIDGET_H
#define LLMCONFIGWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QCheckBox>
#include "core/agent/LLMAgent.h"

class ToolDispatcher;  // 前向声明

class LLMConfigWidget : public QWidget {
    Q_OBJECT
public:
    explicit LLMConfigWidget(QWidget *parent = nullptr);

private slots:
    void onSaveClicked();
    void onSendClicked();
    void onAbortClicked();
    void onFinished(const QString& content);
    void onChunkReceived(const QString& chunk);
    void onToolCallRequested(const QString& toolId, const QString& toolName, const QJsonObject& input);
    void onErrorOccurred(const QString& errorMsg);
    void updateHistoryDisplay();
    void onClearHistoryClicked();
    void onTestToolClicked();
    
    // 阶段二:工具执行生命周期槽函数
    void onToolExecutionStarted(const QString& toolName, const QString& description);
    void onToolExecutionCompleted(const QString& toolName, bool success, const QString& summary);
    
    // 阶段三: 结构化事件处理
    void onToolEvent(const ToolExecutionEvent& event);

private:
    void setupUI();
    void loadConfig();
    void registerTools();  // 注册工具
    
    // UI 辅助函数
    void appendUserMessage(const QString& message);   // 显示用户消息
    void appendAssistantLabel();                      // 显示助手标签
    void setSendingState(bool isSending);             // 设置发送状态

    // UI Widgets
    QLineEdit *m_baseUrlEdit;
    QLineEdit *m_apiKeyEdit;
    QLineEdit *m_modelEdit;
    QTextEdit *m_systemPromptEdit;
    
    QTextBrowser *m_chatDisplay;
    QTextEdit *m_inputEdit;
    
    QPushButton *m_saveBtn;
    QPushButton *m_sendBtn;
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
    QString m_currentAssistantReply;  // 当前助手回复的累积内容
    bool m_pendingAssistantSeparator = false;
};

#endif // LLMCONFIGWIDGET_H
