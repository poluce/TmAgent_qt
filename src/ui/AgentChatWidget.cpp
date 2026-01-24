#include "AgentChatWidget.h"
#include "ToolLogWidget.h"
#include "chat_widget.h"
#include "chat_widget_input.h"
#include "core/agent/ToolDispatcher.h"
#include "core/utils/AppSettings.h"
#include <QDebug>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSplitter>

AgentChatWidget::AgentChatWidget(QWidget* parent)
    : QWidget(parent)
{
    m_agent = new LLMAgent(this);
    m_toolDispatcher = new ToolDispatcher(this);
    m_toolDispatcher->registerDefaultTools(); // 注册默认工具

    // NOTE: 将 ToolDispatcher 传给 Agent，实现自治执行（会自动注册工具）
    m_agent->setToolDispatcher(m_toolDispatcher);

    setupUI();
    loadConfig();

    // 接收到字节流信息
    connect(m_agent, &LLMAgent::streamDataReceived, this, &AgentChatWidget::onStreamDataReceived);
    connect(m_agent, &LLMAgent::finished, this, &AgentChatWidget::onFinished);
    connect(m_agent, &LLMAgent::errorOccurred, this, &AgentChatWidget::onErrorOccurred);
    connect(m_agent, &LLMAgent::toolCallsStarted, this, &AgentChatWidget::onToolCallsStarted);

    // 连接工具事件信号（统一处理 started/completed）
    connect(m_agent, &LLMAgent::toolEvent, this, &AgentChatWidget::onToolEvent);
}

void AgentChatWidget::setupUI()
{
    setWindowTitle("TmAgent - Team of Agents");
    resize(1200, 600); // 扩大窗口宽度以容纳三列

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    // --- 左侧：配置面板 ---
    QWidget* leftContainer = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0); // 消除内边距

    QGroupBox* configGroup = new QGroupBox("LLM 配置", this);
    QFormLayout* formLayout = new QFormLayout(configGroup);

    m_baseUrlEdit = new QLineEdit(this);
    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_modelEdit = new QLineEdit(this);
    m_systemPromptEdit = new QTextEdit(this);
    m_systemPromptEdit->setPlaceholderText("请输入提示词");
    m_systemPromptEdit->setMinimumHeight(150);

    formLayout->addRow("Base URL:", m_baseUrlEdit);
    formLayout->addRow("API Key:", m_apiKeyEdit);
    formLayout->addRow("Model:", m_modelEdit);
    formLayout->addRow("Agent Role:", m_systemPromptEdit);

    m_saveBtn = new QPushButton("保存配置 (Save)", this);
    connect(m_saveBtn, &QPushButton::clicked, this, &AgentChatWidget::onSaveClicked);
    formLayout->addRow(m_saveBtn);

    // 添加工具测试按钮
    m_testToolBtn = new QPushButton("测试工具调用", this);
    m_testToolBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
    connect(m_testToolBtn, &QPushButton::clicked, this, &AgentChatWidget::onTestToolClicked);
    formLayout->addRow(m_testToolBtn);

    // NOTE: 调试模式复选框（UI 自行管理显示模式）
    m_debugModeCheck = new QCheckBox("调试模式", this);
    m_debugModeCheck->setToolTip("启用后在主界面显示简化的工具调用信息");
    connect(m_debugModeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_isDebugMode = checked;
    });
    formLayout->addRow(m_debugModeCheck);

    // NOTE: 添加“查看工具执行日志”按钮
    QPushButton* showLogBtn = new QPushButton("查看工具执行日志 (RAW)", this);
    showLogBtn->setStyleSheet("background-color: #607D8B; color: white; font-weight: bold; padding: 5px;");
    connect(showLogBtn, &QPushButton::clicked, this, [this]() {
        if (!m_toolLogWindow) {
            m_toolLogWindow = new ToolLogWidget(); // 独立顶层窗口
        }
        m_toolLogWindow->show();
        m_toolLogWindow->raise();
        m_toolLogWindow->activateWindow();
    });
    formLayout->addRow(showLogBtn);

    leftLayout->addWidget(configGroup);
    leftLayout->addStretch();

    splitter->addWidget(leftContainer);

    // --- 右侧：交流面板 ---
    QWidget* centerContainer = new QWidget(this);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerContainer);
    centerLayout->setContentsMargins(0, 0, 0, 0);

    m_chatWidget = new ChatWidget(this);
    m_chatWidget->applyStyleSheetFile("chat_widget.qss");
    centerLayout->addWidget(m_chatWidget, 1);

    m_abortBtn = new QPushButton(this);
    m_abortBtn->setEnabled(false);

    splitter->addWidget(centerContainer);

    // --- 右侧:对话历史面板 ---
    QWidget* historyContainer = new QWidget(this);
    QVBoxLayout* historyLayout = new QVBoxLayout(historyContainer);
    historyLayout->setContentsMargins(0, 0, 0, 0);

    m_historyLabel = new QLabel("对话历史 (共 0 轮)", this);
    QFont labelFont = m_historyLabel->font();
    labelFont.setBold(true);
    m_historyLabel->setFont(labelFont);
    historyLayout->addWidget(m_historyLabel);

    m_historyDisplay = new QTextBrowser(this);
    m_historyDisplay->setPlaceholderText("对话历史将在此显示...");
    historyLayout->addWidget(m_historyDisplay, 1);

    m_clearHistoryBtn = new QPushButton("清空历史", this);
    historyLayout->addWidget(m_clearHistoryBtn);

    splitter->addWidget(historyContainer);

    // 设置初始比例：左侧 300px，右侧自适应
    splitter->setStretchFactor(0, 0); // 左侧不拉伸
    splitter->setStretchFactor(1, 1); // 右侧拉伸
    splitter->setSizes(QList<int>() << 320 << 580);

    mainLayout->addWidget(splitter);

    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &AgentChatWidget::onClearHistoryClicked);
    connect(m_chatWidget, &ChatWidget::messageSent, this, &AgentChatWidget::onUserMessageSent);
    connect(m_chatWidget, &ChatWidget::stopRequested, this, &AgentChatWidget::onAbortClicked);
}

void AgentChatWidget::setSendingState(bool isSending)
{
    if (m_chatWidget) {
        m_chatWidget->setSendingState(isSending);
    }
    m_abortBtn->setEnabled(isSending);
    m_testToolBtn->setEnabled(!isSending);
}

void AgentChatWidget::loadConfig()
{
    m_baseUrlEdit->setText(AppSettings::getBaseUrl());
    m_apiKeyEdit->setText(AppSettings::getApiKey());
    m_modelEdit->setText(AppSettings::getModel());
    m_systemPromptEdit->setPlainText(AppSettings::getSystemPrompt());

    // 构造 LLMConfig 并注入 Agent
    LLMConfig config;
    config.apiKey = AppSettings::getApiKey();
    config.baseUrl = AppSettings::getBaseUrl();
    config.model = AppSettings::getModel();
    config.systemPrompt = AppSettings::getSystemPrompt();
    config.temperature = AppSettings::getTemperature();
    m_agent->setConfig(config);
}

void AgentChatWidget::onSaveClicked()
{
    // 保存到 AppSettings
    AppSettings::setBaseUrl(m_baseUrlEdit->text().trimmed());
    AppSettings::setApiKey(m_apiKeyEdit->text().trimmed());
    AppSettings::setModel(m_modelEdit->text().trimmed());
    AppSettings::setSystemPrompt(m_systemPromptEdit->toPlainText().trimmed());

    // 构造 LLMConfig 并注入 Agent
    LLMConfig config;
    config.apiKey = m_apiKeyEdit->text().trimmed();
    config.baseUrl = m_baseUrlEdit->text().trimmed();
    config.model = m_modelEdit->text().trimmed();
    config.systemPrompt = m_systemPromptEdit->toPlainText().trimmed();
    config.temperature = AppSettings::getTemperature();
    m_agent->setConfig(config);

    QMessageBox::information(this, "成功", "配置已成功保存至 config.ini");
}

void AgentChatWidget::onUserMessageSent(const QString& content)
{
    QString prompt = content.trimmed();
    if (prompt.isEmpty())
        return;

    // 清空累积内容和游标
    m_currentAssistantReply.clear();
    m_hasPendingAssistantMessage = false;

    setSendingState(true);

    // 使用 sendMessage，已注册工具会自动附带
    m_agent->sendMessage(prompt);
}

void AgentChatWidget::onAbortClicked()
{
    qDebug() << "------------------------------------------";
    qDebug() << "AgentChatWidget: [Signal Received] Stop requested by User UI";
    m_agent->abort();

    if (m_chatWidget) {
        m_chatWidget->addMessage("[已手动中断]", false, "System");
    }
    m_hasPendingAssistantMessage = false;
    setSendingState(false);
}

void AgentChatWidget::onStreamDataReceived(const QString& data)
{
    if (!m_chatWidget)
        return;

    if (!m_hasPendingAssistantMessage) {
        m_chatWidget->addMessage("", false, "TM Agent");
        m_hasPendingAssistantMessage = true;
    }
    m_currentAssistantReply += data;
    m_chatWidget->streamOutput(data);
}

void AgentChatWidget::onToolCallsStarted()
{
    if (!m_chatWidget)
        return;

    if (m_hasPendingAssistantMessage) {
        m_chatWidget->removeLastMessage();
        m_hasPendingAssistantMessage = false;
        m_currentAssistantReply.clear();
    }
}

void AgentChatWidget::onFinished(const QString& fullContent)
{
    if (!m_chatWidget)
        return;

    if (!m_hasPendingAssistantMessage && !fullContent.isEmpty()) {
        m_chatWidget->addMessage(fullContent, false, "TM Agent");
    }
    m_hasPendingAssistantMessage = false;
    m_currentAssistantReply.clear();
    updateHistoryDisplay();
    setSendingState(false);
}

void AgentChatWidget::updateHistoryDisplay()
{
    QJsonArray history = m_agent->getHistory();
    int count = m_agent->getConversationCount();

    m_historyLabel->setText(QString("对话历史 (共 %1 轮)").arg(count));

    if (history.isEmpty()) {
        m_historyDisplay->clear();
        return;
    }

    QString htmlContent;
    int roundNum = 0;

    for (int i = 0; i < history.size(); i++) {
        QJsonObject msg = history[i].toObject();
        QString role = msg["role"].toString();
        QString content = msg["content"].toString();

        if (role == "user") {
            roundNum++;
            htmlContent += QString("<p><b>第 %1 轮:</b></p>").arg(roundNum);
            htmlContent += QString("<p style='color: blue;'><b>User:</b> %1</p>").arg(content.toHtmlEscaped());
        } else if (role == "assistant") {
            htmlContent += QString("<p style='color: green;'><b>Assistant:</b> %1</p><br>").arg(content.toHtmlEscaped());
        }
    }

    m_historyDisplay->setHtml(htmlContent);
}

void AgentChatWidget::onClearHistoryClicked()
{
    m_agent->clearHistory();
    m_historyDisplay->clear();
    m_historyLabel->setText("对话历史 (共 0 轮)");
    if (m_chatWidget) {
        m_chatWidget->addMessage("[对话历史已清空]", false, "System");
    }
}

// ==================== 工具调用相关 ====================

void AgentChatWidget::onTestToolClicked()
{
    // 清空累积内容
    m_currentAssistantReply.clear();
    m_hasPendingAssistantMessage = false;

    // 显示测试消息
    QString testPrompt = "请在 E:/test 目录下创建一个名为 helloworld.txt 的文件,内容是 'Hello from DeepSeek Tool Calling!'";
    if (m_chatWidget) {
        m_chatWidget->addMessage(QString("🔧 工具调用测试: %1").arg(testPrompt), true, "Me");
    }
    setSendingState(true);

    // 使用 sendMessage 发起工具调用
    m_agent->sendMessage(testPrompt);
}

void AgentChatWidget::onErrorOccurred(const QString& errorMsg)
{
    if (m_chatWidget) {
        m_chatWidget->addMessage(QString("❌ 错误: %1").arg(errorMsg), false, "System");
    }

    m_hasPendingAssistantMessage = false;
    setSendingState(false);
}

// ==================== 工具事件处理 ====================

void AgentChatWidget::onToolEvent(const ToolExecutionEvent& event)
{
    // 1. 同步到独立日志窗口
    if (m_toolLogWindow) {
        m_toolLogWindow->logEvent(event);
    }

    // 2. 主界面简化处理
    if (!m_chatWidget)
        return;

    if (m_isDebugMode) {
        if (event.status == "started") {
            m_chatWidget->addMessage(QString("⚡ 正在执行工具: %1").arg(event.toolName), false, "Tool");
        } else if (event.status == "progress") {
            m_chatWidget->addMessage(QString("⏳ %1: %2").arg(event.toolName, event.formattedResult), false, "Tool");
        } else if (event.status == "completed") {
            QString icon = event.success ? "✅" : "❌";
            m_chatWidget->addMessage(
                QString("%1 %2 完成: %3").arg(icon, event.toolName, event.formattedResult),
                false,
                "Tool");
        }
        return;
    }

    if (event.status == "progress") {
        m_chatWidget->addMessage(QString("⏳ %1: %2").arg(event.toolName, event.formattedResult), false, "Tool");
        return;
    }

    if (event.status == "completed" && !event.success) {
        m_chatWidget->addMessage(QString("❌ %1 执行失败").arg(event.toolName), false, "Tool");
    }
}
