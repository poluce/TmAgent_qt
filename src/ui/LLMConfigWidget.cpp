#include "LLMConfigWidget.h"
#include "core/utils/ConfigManager.h"
#include "core/agent/ToolDispatcher.h"
#include <QHBoxLayout>
#include <QMessageBox>
#include <QGroupBox>
#include <QSplitter>
#include <QTextCursor>
#include <QTextDocument>

LLMConfigWidget::LLMConfigWidget(QWidget *parent) : QWidget(parent) {
    m_agent = new LLMAgent(this);
    m_toolDispatcher = new ToolDispatcher(this);
    
    setupUI();
    loadConfig();
    registerTools();  // 注册工具

    // 连接基础信号
    connect(m_agent, &LLMAgent::chunkReceived, this, &LLMConfigWidget::onChunkReceived);
    connect(m_agent, &LLMAgent::finished, this, &LLMConfigWidget::onFinished);
    connect(m_agent, &LLMAgent::errorOccurred, this, &LLMConfigWidget::onErrorOccurred);
    
    // 连接工具调用信号
    connect(m_agent, &LLMAgent::toolCallRequested, this, &LLMConfigWidget::onToolCallRequested);
    
    // NOTE: 连接工具执行生命周期信号
    connect(m_agent, &LLMAgent::toolExecutionStarted, 
            this, &LLMConfigWidget::onToolExecutionStarted);
    connect(m_agent, &LLMAgent::toolExecutionCompleted, 
            this, &LLMConfigWidget::onToolExecutionCompleted);
    
    // NOTE: 连接结构化事件信号
    connect(m_agent, &LLMAgent::toolEvent, this, &LLMConfigWidget::onToolEvent);
}

void LLMConfigWidget::setupUI() {
    setWindowTitle("DeepSeek LLM 配置与验证");
    resize(1200, 600);  // 扩大窗口宽度以容纳三列

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    // --- 左侧：配置面板 ---
    QWidget *leftContainer = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0); // 消除内边距

    QGroupBox *configGroup = new QGroupBox("LLM 配置", this);
    QFormLayout *formLayout = new QFormLayout(configGroup);

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
    connect(m_saveBtn, &QPushButton::clicked, this, &LLMConfigWidget::onSaveClicked);
    formLayout->addRow(m_saveBtn);
    
    // 添加工具测试按钮
    m_testToolBtn = new QPushButton("🔧 测试工具调用", this);
    m_testToolBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
    connect(m_testToolBtn, &QPushButton::clicked, this, &LLMConfigWidget::onTestToolClicked);
    formLayout->addRow(m_testToolBtn);
    
    // NOTE: 添加调试模式复选框
    m_debugModeCheck = new QCheckBox("📝 调试模式", this);
    m_debugModeCheck->setToolTip("启用后显示详细的工具调用信息");
    connect(m_debugModeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_agent->setOutputMode(checked ? Debug : UserFriendly);
        m_chatDisplay->append(QString("<p style='color: #666;'><i>已切换到%1模式</i></p>")
            .arg(checked ? "调试" : "用户友好"));
    });
    formLayout->addRow(m_debugModeCheck);

    leftLayout->addWidget(configGroup);
    leftLayout->addStretch();
    
    splitter->addWidget(leftContainer);

    // --- 右侧：交流面板 ---
    QWidget *centerContainer = new QWidget(this);
    QVBoxLayout *centerLayout = new QVBoxLayout(centerContainer);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    
    m_chatDisplay = new QTextBrowser(this);
    m_chatDisplay->setPlaceholderText("交流内容显示区...");
    centerLayout->addWidget(m_chatDisplay, 1);

    // 输入区
    QHBoxLayout *inputLayout = new QHBoxLayout();
    m_inputEdit = new QTextEdit(this);
    m_inputEdit->setMaximumHeight(100);
    m_inputEdit->setPlaceholderText("在此输入问题，按“发送”开始交流...");
    
    QVBoxLayout *btnLayout = new QVBoxLayout();
    m_sendBtn = new QPushButton("发送 (Send)", this);
    m_abortBtn = new QPushButton("停止 (Abort)", this);
    m_abortBtn->setEnabled(false);
    
    btnLayout->addWidget(m_sendBtn);
    btnLayout->addWidget(m_abortBtn);
    
    inputLayout->addWidget(m_inputEdit);
    inputLayout->addLayout(btnLayout);
    
    centerLayout->addLayout(inputLayout);
    
    splitter->addWidget(centerContainer);

    // --- 右侧:对话历史面板 ---
    QWidget *historyContainer = new QWidget(this);
    QVBoxLayout *historyLayout = new QVBoxLayout(historyContainer);
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

    connect(m_sendBtn, &QPushButton::clicked, this, &LLMConfigWidget::onSendClicked);
    connect(m_abortBtn, &QPushButton::clicked, this, &LLMConfigWidget::onAbortClicked);
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &LLMConfigWidget::onClearHistoryClicked);
}

// ==================== UI 辅助函数 ====================

void LLMConfigWidget::appendUserMessage(const QString& message) {
    m_chatDisplay->append("<br>");
    m_chatDisplay->append("<b style='color: #2196F3;'>User:</b>");
    m_chatDisplay->append("<p>" + message.toHtmlEscaped() + "</p>");
}

void LLMConfigWidget::appendAssistantLabel() {
    m_chatDisplay->append("<b style='color: #4CAF50;'>Assistant:</b>");
}

void LLMConfigWidget::setSendingState(bool isSending) {
    m_sendBtn->setEnabled(!isSending);
    m_abortBtn->setEnabled(isSending);
    m_testToolBtn->setEnabled(!isSending);
    
    if (!isSending) {
        m_inputEdit->clear();
    }
}

void LLMConfigWidget::loadConfig() {
    m_baseUrlEdit->setText(ConfigManager::getBaseUrl());
    m_apiKeyEdit->setText(ConfigManager::getApiKey());
    m_modelEdit->setText(ConfigManager::getModel());
    m_systemPromptEdit->setPlainText(ConfigManager::getSystemPrompt());
    
    // 同步 Agent 的角色设定
    m_agent->setSystemPrompt(ConfigManager::getSystemPrompt());
}

void LLMConfigWidget::onSaveClicked() {
    ConfigManager::setBaseUrl(m_baseUrlEdit->text().trimmed());
    ConfigManager::setApiKey(m_apiKeyEdit->text().trimmed());
    ConfigManager::setModel(m_modelEdit->text().trimmed());
    ConfigManager::setSystemPrompt(m_systemPromptEdit->toPlainText().trimmed());
    
    // 同步 Agent 的角色设定
    m_agent->setSystemPrompt(m_systemPromptEdit->toPlainText().trimmed());
    
    QMessageBox::information(this, "成功", "配置已成功保存至 config.ini");
}

void LLMConfigWidget::onSendClicked() {
    QString prompt = m_inputEdit->toPlainText().trimmed();
    if (prompt.isEmpty()) return;

    // 清空累积内容
    m_currentAssistantReply.clear();
    m_pendingAssistantSeparator = false;

    // 显示用户消息
    appendUserMessage(prompt);
    setSendingState(true);
    
    // 使用 sendMessage，已注册工具会自动附带
    m_agent->sendMessage(prompt);
}

void LLMConfigWidget::onAbortClicked() {
    m_agent->abort();
    m_chatDisplay->append("<br><i>[已中断]</i>");
    setSendingState(false);
}

void LLMConfigWidget::onChunkReceived(const QString& chunk) {
    // 首次收到 chunk 时显示 Assistant 标签
    if (m_currentAssistantReply.isEmpty()) {
        if (m_pendingAssistantSeparator) {
            // 工具日志与助手回复之间加一行，避免粘连
            m_chatDisplay->append("");
            m_pendingAssistantSeparator = false;
        }
        appendAssistantLabel();
    }
    
    m_currentAssistantReply += chunk;
    
    // 实时显示纯文本(流式效果)
    m_chatDisplay->insertPlainText(chunk);
    m_chatDisplay->ensureCursorVisible();
}

void LLMConfigWidget::onFinished(const QString& fullContent) {
    qDebug() << "========== onFinished 被调用 ==========";
    qDebug() << "内容:" << fullContent;
    qDebug() << "当前累积内容长度:" << m_currentAssistantReply.length();
    
    Q_UNUSED(fullContent);
    
    // 将累积的纯文本替换为 Markdown 渲染
    if (!m_currentAssistantReply.isEmpty()) {
        QTextCursor cursor = m_chatDisplay->textCursor();
        cursor.movePosition(QTextCursor::End);
        
        // 向前删除刚才插入的纯文本
        for (int i = 0; i < m_currentAssistantReply.length(); i++) {
            cursor.deletePreviousChar();
        }
        
        // 使用 QTextDocument 渲染 Markdown
        QTextDocument doc;
        doc.setMarkdown(m_currentAssistantReply);
        
        // 插入渲染后的 HTML
        cursor.insertHtml(doc.toHtml());
        m_chatDisplay->setTextCursor(cursor);
    } else {
        // 工具调用模式下,可能没有累积内容,直接显示 fullContent
        if (!fullContent.isEmpty()) {
            m_chatDisplay->append(fullContent);
        }
    }
    
    qDebug() << "恢复按钮状态...";
    setSendingState(false);
    qDebug() << "按钮状态已恢复";
    
}

void LLMConfigWidget::updateHistoryDisplay() {
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

void LLMConfigWidget::onClearHistoryClicked() {
    m_agent->clearHistory();
    m_historyDisplay->clear();
    m_historyLabel->setText("对话历史 (共 0 轮)");
    m_chatDisplay->append("<br><i>[对话历史已清空]</i>");
}

// ==================== 工具调用相关 ====================

void LLMConfigWidget::registerTools() {
    // 从 ToolDispatcher 获取所有工具的 Schema 定义
    QList<Tool> tools = ToolDispatcher::getAllToolSchemas();
    
    for (const Tool& tool : tools) {
        m_agent->registerTool(tool);
        qDebug() << "已注册工具:" << tool.name;
    }
    
    qDebug() << "共注册" << tools.size() << "个工具";
}

void LLMConfigWidget::onTestToolClicked() {
    // 清空累积内容
    m_currentAssistantReply.clear();
    m_pendingAssistantSeparator = false;
    
    // 显示测试消息
    QString testPrompt = "请在 E:/test 目录下创建一个名为 helloworld.txt 的文件,内容是 'Hello from DeepSeek Tool Calling!'";
    m_chatDisplay->append("<br>");
    m_chatDisplay->append("<b style='color: #FF9800;'>🔧 工具调用测试:</b>");
    m_chatDisplay->append("<p>" + testPrompt + "</p>");
    setSendingState(true);
    
    // 使用 sendMessage 发起工具调用
    m_agent->sendMessage(testPrompt);
}

void LLMConfigWidget::onToolCallRequested(const QString& toolId, 
                                          const QString& toolName,
                                          const QJsonObject& input) {
    bool isDebugMode = m_agent->outputMode() == Debug;
    // 用户友好模式也显示具体命令，避免“黑盒”感
    if (toolName == "execute_command") {
        const QString command = input.value("command").toString();
        const QString cwd = input.value("working_directory").toString();
        QString line = QString("<p style='color: #666;'>🔧 执行命令: <code>%1</code></p>")
                           .arg(command.toHtmlEscaped());
        if (!cwd.isEmpty()) {
            line += QString("<p style='color: #999; margin-top: -6px;'>目录: <code>%1</code></p>")
                        .arg(cwd.toHtmlEscaped());
        }
        m_chatDisplay->append(line);
        m_pendingAssistantSeparator = true;
    }
    
    if (isDebugMode) {
        // 显示工具调用信息
        m_chatDisplay->append("<br>");
        m_chatDisplay->append("<b style='color: #9C27B0;'>🔧 工具调用:</b>");
        m_chatDisplay->append(QString("<p>工具: <b>%1</b></p>").arg(toolName));
        m_chatDisplay->append(QString("<p>参数: <code>%1</code></p>")
                             .arg(QString(QJsonDocument(input).toJson(QJsonDocument::Compact))));
        
        m_chatDisplay->append("<p>⏳ 正在执行...</p>");
        m_pendingAssistantSeparator = true;
    }
    
    // 使用 ToolDispatcher 执行工具（解耦：UI 不再直接调用工具）
    QString result = m_toolDispatcher->dispatch(toolName, input);
    
    if (isDebugMode) {
        // 显示结果摘要（限制长度）
        QString displayResult = result;
        if (displayResult.length() > 500) {
            displayResult = displayResult.left(250) 
                          + "\n\n... (内容过长，已截断显示) ...\n\n" 
                          + displayResult.right(200);
        }
        
        bool success = !result.contains("错误") && !result.contains("失败");
        QString color = success ? "#28a745" : "#dc3545";
        
        m_chatDisplay->append(QString("<p>→ 结果:</p><pre style='background: #f5f5f5; padding: 10px; border-radius: 5px; border-left: 3px solid %1; white-space: pre-wrap;'>%2</pre>")
                             .arg(color)
                             .arg(displayResult.toHtmlEscaped()));
        m_pendingAssistantSeparator = true;
    }
    
    // 返回结果给 Agent
    m_agent->submitToolResult(toolId, result);
}

// ==================== 阶段二:工具执行生命周期槽函数 ====================

void LLMConfigWidget::onToolExecutionStarted(const QString& toolName, const QString& description) {
    bool isDebugMode = m_agent->outputMode() == Debug;
    if (!isDebugMode && toolName == "execute_command") {
        return;
    }
    // 显示简洁的工具执行开始提示
    QString html = QString("<p style='color: #888; font-style: italic; margin: 5px 0;'>🔧 %1...</p>")
                   .arg(description);
    m_chatDisplay->append(html);
    m_pendingAssistantSeparator = true;
}

void LLMConfigWidget::onToolExecutionCompleted(const QString& toolName, bool success, const QString& summary) {
    Q_UNUSED(toolName);
    // 显示格式化的结果摘要
    QString icon = success ? "✅" : "❌";
    QString color = success ? "#28a745" : "#dc3545";
    QString html = QString("<p style='color: %1; margin: 5px 0;'>%2 %3</p>")
                   .arg(color)
                   .arg(icon)
                   .arg(summary);
    m_chatDisplay->append(html);
    m_pendingAssistantSeparator = true;
}

void LLMConfigWidget::onErrorOccurred(const QString& errorMsg) {
    m_chatDisplay->append(QString("<p style='color: red;'>❌ 错误: %1</p>").arg(errorMsg));
    
    // 恢复按钮状态
    m_sendBtn->setEnabled(true);
    m_abortBtn->setEnabled(false);
}

// ==================== 阶段三: 结构化事件处理 ====================

void LLMConfigWidget::onToolEvent(const ToolExecutionEvent& event) {
    // 根据当前输出模式选择显示内容
    bool isDebugMode = m_agent->outputMode() == Debug;
    
    if (event.status == "started") {
        // 工具开始执行
        if (isDebugMode) {
            // 调试模式: 显示详细信息
            QString html = QString(
                "<div style='background: #f0f0f0; padding: 8px; margin: 5px 0; border-left: 3px solid #2196F3;'>"
                "<b>🔧 工具调用开始</b><br>"
                "<b>工具名:</b> %1<br>"
                "<b>详细信息:</b> <code>%2</code>"
                "</div>")
                .arg(event.toolName)
                .arg(event.debugMessage.toHtmlEscaped());
            m_chatDisplay->append(html);
        }
        // 用户友好模式下不显示 started 事件（避免重复，阶段二的信号已处理）
        
    } else if (event.status == "completed") {
        // 工具执行完成
        if (isDebugMode) {
            // 调试模式: 显示完整结果
            QString borderColor = event.success ? "#28a745" : "#dc3545";
            QString icon = event.success ? "✅" : "❌";
            QString html = QString(
                "<div style='background: #f8f9fa; padding: 8px; margin: 5px 0; border-left: 3px solid %1;'>"
                "<b>%2 工具执行完成</b><br>"
                "<b>工具名:</b> %3<br>"
                "<b>用户消息:</b> %4<br>"
                "<b>原始结果:</b><br><pre style='background: #eee; padding: 5px;'>%5</pre>"
                "</div>")
                .arg(borderColor)
                .arg(icon)
                .arg(event.toolName)
                .arg(event.userMessage.toHtmlEscaped())
                .arg(event.debugMessage.toHtmlEscaped());
            m_chatDisplay->append(html);
        }
        // 用户友好模式下不显示 completed 事件（阶段二的信号已处理）
    }
    
    m_chatDisplay->ensureCursorVisible();
}
