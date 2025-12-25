#include "AgentChatWidget.h"
#include "core/utils/AppSettings.h"
#include "core/agent/ToolDispatcher.h"
#include <QHBoxLayout>
#include <QMessageBox>
#include <QGroupBox>
#include <QSplitter>
#include <QTextCursor>
#include <QTextDocument>

AgentChatWidget::AgentChatWidget(QWidget *parent) : QWidget(parent) {
    m_agent = new LLMAgent(this);
    m_toolDispatcher = new ToolDispatcher(this);
    m_toolDispatcher->registerDefaultTools();  // 注册默认工具
    
    // NOTE: 将 ToolDispatcher 传给 Agent，实现自治执行（会自动注册工具）
    m_agent->setToolDispatcher(m_toolDispatcher);
    
    setupUI();
    loadConfig();

    // 接收到字节流信息
    connect(m_agent, &LLMAgent::streamDataReceived, this, &AgentChatWidget::onStreamDataReceived);
    connect(m_agent, &LLMAgent::finished, this, &AgentChatWidget::onFinished);
    connect(m_agent, &LLMAgent::errorOccurred, this, &AgentChatWidget::onErrorOccurred);
    
    // 连接工具事件信号（统一处理 started/completed）
    connect(m_agent, &LLMAgent::toolEvent, this, &AgentChatWidget::onToolEvent);
}

void AgentChatWidget::setupUI() {
    setWindowTitle("TmAgent - Team of Agents");
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
    connect(m_saveBtn, &QPushButton::clicked, this, &AgentChatWidget::onSaveClicked);
    formLayout->addRow(m_saveBtn);
    
    // 添加工具测试按钮
    m_testToolBtn = new QPushButton("测试工具调用", this);
    m_testToolBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
    connect(m_testToolBtn, &QPushButton::clicked, this, &AgentChatWidget::onTestToolClicked);
    formLayout->addRow(m_testToolBtn);
    
    // NOTE: 调试模式复选框（UI 自行管理显示模式）
    m_debugModeCheck = new QCheckBox("调试模式", this);
    m_debugModeCheck->setToolTip("启用后显示详细的工具调用信息");
    connect(m_debugModeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_isDebugMode = checked;
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

    connect(m_sendBtn, &QPushButton::clicked, this, &AgentChatWidget::onSendClicked);
    connect(m_abortBtn, &QPushButton::clicked, this, &AgentChatWidget::onAbortClicked);
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &AgentChatWidget::onClearHistoryClicked);
}

// ==================== UI 辅助函数 ====================

void AgentChatWidget::appendUserMessage(const QString& message) {
    m_chatDisplay->append("<br>");
    m_chatDisplay->append("<b style='color: #2196F3;'>User:</b>");
    m_chatDisplay->append("<p>" + message.toHtmlEscaped() + "</p>");
}

void AgentChatWidget::appendAssistantLabel() {
    m_chatDisplay->append("<b style='color: #4CAF50;'>Assistant:</b>\n");
}

void AgentChatWidget::setSendingState(bool isSending) {
    m_sendBtn->setEnabled(!isSending);
    m_abortBtn->setEnabled(isSending);
    m_testToolBtn->setEnabled(!isSending);
    
    if (!isSending) {
        m_inputEdit->clear();
    }
}

void AgentChatWidget::loadConfig() {
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

void AgentChatWidget::onSaveClicked() {
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

void AgentChatWidget::onSendClicked() {
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

void AgentChatWidget::onAbortClicked() {
    m_agent->abort();
    m_chatDisplay->append("<br><i>[已中断]</i>");
    setSendingState(false);
}

void AgentChatWidget::onStreamDataReceived(const QString& data) {
    // 首次收到数据时处理分隔和标签
    if (m_currentAssistantReply.isEmpty()) {
        if (m_pendingAssistantSeparator) {
            // 工具日志与助手回复之间加一行，避免粘连
            m_chatDisplay->append("\n");
            m_pendingAssistantSeparator = false;
        }
        
        // 检测 LLM 是否自带 "Assistant:" 前缀，避免重复
        if (!data.trimmed().startsWith("Assistant:")) {
            appendAssistantLabel();
        }
    }
    
    m_currentAssistantReply += data;
    
    // 实时显示纯文本(流式效果)
    QTextCursor cursor = m_chatDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_chatDisplay->setTextCursor(cursor);
    
    m_chatDisplay->insertPlainText(data);
    m_chatDisplay->ensureCursorVisible();
}

void AgentChatWidget::onFinished(const QString& fullContent) {
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

void AgentChatWidget::updateHistoryDisplay() {
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

void AgentChatWidget::onClearHistoryClicked() {
    m_agent->clearHistory();
    m_historyDisplay->clear();
    m_historyLabel->setText("对话历史 (共 0 轮)");
    m_chatDisplay->append("<br><i>[对话历史已清空]</i>");
}

// ==================== 工具调用相关 ====================

void AgentChatWidget::onTestToolClicked() {
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


void AgentChatWidget::onErrorOccurred(const QString& errorMsg) {
    m_chatDisplay->append(QString("<p style='color: red;'>❌ 错误: %1</p>").arg(errorMsg));
    
    // 恢复按钮状态
    m_sendBtn->setEnabled(true);
    m_abortBtn->setEnabled(false);
}

// ==================== 工具事件处理 ====================

void AgentChatWidget::onToolEvent(const ToolExecutionEvent& event) {
    if (event.status == "started") {
        // 工具开始执行
        if (m_isDebugMode) {
            // 调试模式: 显示详细信息
            QString html = QString(
                "<div style='background: #f0f0f0; padding: 8px; margin: 5px 0; border-left: 3px solid #2196F3;'>"
                "<b>🔧 工具调用开始</b><br>"
                "<b>工具名:</b> %1<br>"
                "<b>详细信息:</b> <code>%2</code>"
                "</div>")
                .arg(event.toolName)
                .arg(event.debugMessage().toHtmlEscaped());
            m_chatDisplay->append(html);
        } else {
            // 用户友好模式: 显示简洁提示
            QString html = QString("<p style='color: #888; font-style: italic; margin: 5px 0;'>🔧 %1</p>")
                           .arg(event.userMessage());
            m_chatDisplay->append(html);
        }
        m_pendingAssistantSeparator = true;
        
    } else if (event.status == "completed") {
        // 工具执行完成
        QString icon = event.success ? "✅" : "❌";
        QString borderColor = event.success ? "#28a745" : "#dc3545";
        
        if (m_isDebugMode) {
            // 调试模式: 显示完整结果
            QString html = QString(
                "<div style='background: #f8f9fa; padding: 8px; margin: 5px 0; border-left: 3px solid %1;'>"
                "<b>%2 工具执行完成</b><br>"
                "<b>工具名:</b> %3<br>"
                "<b>结果:</b> %4<br>"
                "<b>原始输出:</b><br><pre style='background: #eee; padding: 5px;'>%5</pre>"
                "</div>")
                .arg(borderColor)
                .arg(icon)
                .arg(event.toolName)
                .arg(event.userMessage().toHtmlEscaped())
                .arg(event.debugMessage().toHtmlEscaped());
            m_chatDisplay->append(html);
        } else {
            // 用户友好模式: 显示简洁结果
            QString html = QString("<p style='color: %1; margin: 5px 0;'>%2 %3</p>")
                           .arg(borderColor)
                           .arg(icon)
                           .arg(event.userMessage());
            m_chatDisplay->append(html);
        }
        m_pendingAssistantSeparator = true;
    }
    
    m_chatDisplay->ensureCursorVisible();
}

