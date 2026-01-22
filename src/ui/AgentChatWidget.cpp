#include "AgentChatWidget.h"
#include "core/utils/AppSettings.h"
#include "core/agent/ToolDispatcher.h"
#include "ToolLogWidget.h"
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
    m_debugModeCheck->setToolTip("启用后在主界面显示简化的工具调用信息");
    connect(m_debugModeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_isDebugMode = checked;
    });
    formLayout->addRow(m_debugModeCheck);
    
    // NOTE: 添加“查看工具执行日志”按钮
    QPushButton *showLogBtn = new QPushButton("查看工具执行日志 (RAW)", this);
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

// ==================== UI 辅助函数 ====================

void AgentChatWidget::appendUserMessage(const QString& message) {
    m_chatDisplay->moveCursor(QTextCursor::End);
    // 极致简约：去除背景、边框和表格
    QString html = QString(
        "<div style='margin-top: 30px; margin-bottom: 20px; font-family: \"Microsoft YaHei\", sans-serif;'>"
        "  <div style='color: #0078d4; font-weight: bold; font-size: 11px; margin-bottom: 8px;'>● YOU</div>"
        "  <div style='color: #222; font-size: 14px; line-height: 1.6;'>%1</div>"
        "</div>")
        .arg(message.toHtmlEscaped().replace("\n", "<br>"));
    m_chatDisplay->insertHtml(html);
    m_chatDisplay->append(""); // 强制开启新段落，确保下一条消息不粘连
}

void AgentChatWidget::appendAssistantLabel() {
    m_chatDisplay->moveCursor(QTextCursor::End);
    // 记录每轮回复开始的确切位置，用于后续完成覆盖
    m_assistantTurnCursor = m_chatDisplay->textCursor();
    
    QString html = 
        "<div style='margin-top: 25px; margin-bottom: 8px; color: #388e3c; font-weight: bold; font-size: 11px;'>"
        "● TM AGENT"
        "</div>";
    m_chatDisplay->insertHtml(html);
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

    // 清空累积内容和游标
    m_currentAssistantReply.clear();
    m_pendingAssistantSeparator = false;
    m_toolStatusCursors.clear();
    m_assistantTurnCursor = QTextCursor(); // 重置游标

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
    Q_UNUSED(fullContent);
    
    // 核心修复：彻底替换流式传输期间产生的所有临时文本
    if (!m_currentAssistantReply.isEmpty() && !m_assistantTurnCursor.isNull()) {
        QTextCursor cursor = m_chatDisplay->textCursor();
        // 设置选区：从开始标识符之后到末尾
        cursor.setPosition(m_assistantTurnCursor.position());
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        
        QTextDocument doc;
        doc.setMarkdown(m_currentAssistantReply);
        
        // 重新包装：包含标识符 + 渲染后的 Markdown 内容
        QString html = QString(
            "<div style='margin-top: 25px; margin-bottom: 8px; color: #388e3c; font-weight: bold; font-size: 11px;'> ● TM AGENT </div>"
            "<div style='margin-top: 4px; margin-bottom: 20px; color: #333; font-size: 13px; line-height: 1.6;'>"
            "%1"
            "</div>")
            .arg(doc.toHtml());
            
        cursor.insertHtml(html);
        m_chatDisplay->setTextCursor(cursor);
    } else if (!fullContent.isEmpty()) {
        m_chatDisplay->append(QString("<div style='margin-top: 4px; color: #666; font-size: 13px;'>%1</div>").arg(fullContent.toHtmlEscaped()));
    }
    
    setSendingState(false);
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
    m_toolStatusCursors.clear();
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
    // 1. 同步到独立日志窗口
    if (m_toolLogWindow) {
        m_toolLogWindow->logEvent(event);
    }
    
    // 2. 主界面动态处理 (Antigravity 风格)
    if (event.status == "started") {
        m_chatDisplay->moveCursor(QTextCursor::End);
        QTextCursor cursor = m_chatDisplay->textCursor();
        
        // 记录开始位置
        int start = cursor.position();
        
        // 插入一个带有背景和边框的状态框
        QString html = QString(
            "<div style='background-color: #f5f5f5; color: #666; border: 1px dashed #ccc; "
            "padding: 10px; margin: 10px 0; font-family: Consolas; font-size: 12px;'>"
            "<span style='color: #0078d4; font-weight: bold;'>⚡ 正在执行工具:</span> %1"
            "</div>")
            .arg(event.toolName);
            
        cursor.insertHtml(html);
        int end = cursor.position();
        
        // 选中刚才插入的内容并保存游标副本
        QTextCursor persistentCursor = cursor;
        persistentCursor.setPosition(start);
        persistentCursor.setPosition(end, QTextCursor::KeepAnchor);
        m_toolStatusCursors[event.toolId] = persistentCursor;
        
        m_pendingAssistantSeparator = true;
        
    } else if (event.status == "completed") {
        if (m_toolStatusCursors.contains(event.toolId)) {
            QTextCursor cursor = m_toolStatusCursors[event.toolId];
            
            if (m_isDebugMode) {
                // 调试模式: 替换为详细结果
                // 注意：在原有区域进行替换
                QString icon = event.success ? "✅" : "❌";
                QString color = event.success ? "#28a745" : "#dc3545";
                QString html = QString(
                    "<div style='background: #f8f9fa; padding: 8px; margin: 5px 0; border-left: 3px solid %1;'>"
                    "<b>%2 %3 已完成</b><br>"
                    "<span style='color: #888; font-size: 10px;'>ID: %4</span><br>"
                    "<div style='color: #333; margin-top: 5px;'>摘要: %5</div>"
                    "</div>")
                    .arg(color, icon, event.toolName, event.toolId, event.formattedResult.toHtmlEscaped());
                cursor.insertHtml(html);
            } else {
                // 普通模式: 如果成功则直接擦除（Antigravity 风格：结束即关闭）
                if (event.success) {
                    cursor.removeSelectedText();
                } else {
                    // 如果失败，保留并高亮错误提示
                    QString html = QString("<p style='color: #dc3545; margin: 5px 0; font-size: 11px;'>❌ %1 执行失败</p>")
                                   .arg(event.toolName);
                    cursor.insertHtml(html);
                }
            }
            m_toolStatusCursors.remove(event.toolId);
        }
    }
    
    m_chatDisplay->ensureCursorVisible();
}

