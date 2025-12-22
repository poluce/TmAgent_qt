#include "LLMConfigWidget.h"
#include "core/utils/ConfigManager.h"
#include "core/tools/FileTool.h"
#include "core/tools/ShellTool.h"
#include <QHBoxLayout>
#include <QMessageBox>
#include <QGroupBox>
#include <QSplitter>
#include <QTextCursor>
#include <QTextDocument>

LLMConfigWidget::LLMConfigWidget(QWidget *parent) : QWidget(parent) {
    m_agent = new LLMAgent(this);
    
    setupUI();
    loadConfig();
    registerTools();  // 注册工具

    connect(m_agent, &LLMAgent::chunkReceived, this, &LLMConfigWidget::onChunkReceived);
    connect(m_agent, &LLMAgent::finished, this, &LLMConfigWidget::onFinished);
    connect(m_agent, &LLMAgent::errorOccurred, this, &LLMConfigWidget::onError);
    
    // 连接工具调用信号
    connect(m_agent, &LLMAgent::toolCallRequested, this, &LLMConfigWidget::onToolCallRequested);
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
    m_systemPromptEdit->setPlaceholderText("请输入智能体的人格设定...");
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

void LLMConfigWidget::loadConfig() {
    m_baseUrlEdit->setText(ConfigManager::getBaseUrl());
    m_apiKeyEdit->setText(ConfigManager::getApiKey());
    m_modelEdit->setText(ConfigManager::getModel());
    m_systemPromptEdit->setPlainText(ConfigManager::getSystemPrompt());
}

void LLMConfigWidget::onSaveClicked() {
    ConfigManager::setBaseUrl(m_baseUrlEdit->text().trimmed());
    ConfigManager::setApiKey(m_apiKeyEdit->text().trimmed());
    ConfigManager::setModel(m_modelEdit->text().trimmed());
    ConfigManager::setSystemPrompt(m_systemPromptEdit->toPlainText().trimmed());
    QMessageBox::information(this, "成功", "配置已成功保存至 config.ini");
}

void LLMConfigWidget::onSendClicked() {
    QString prompt = m_inputEdit->toPlainText().trimmed();
    if (prompt.isEmpty()) return;

    // 更新 Agent 的角色设定(不保存到配置文件)
    m_agent->setSystemPrompt(m_systemPromptEdit->toPlainText().trimmed());

    // 清空累积内容
    m_currentAssistantReply.clear();

    // 显示用户消息
    m_chatDisplay->append("<br>");
    m_chatDisplay->append("<b style='color: #2196F3;'>User:</b>");
    m_chatDisplay->append("<p>" + prompt.toHtmlEscaped() + "</p>");
    m_chatDisplay->append("<b style='color: #4CAF50;'>Assistant:</b>");
    
    m_sendBtn->setEnabled(false);
    m_abortBtn->setEnabled(true);
    
    // 使用 askWithTools 而不是 ask,这样才会发送工具定义
    m_agent->askWithTools(prompt);
}

void LLMConfigWidget::onAbortClicked() {
    m_agent->abort();
    m_chatDisplay->append("<br><i>[已中断]</i>");
    
    m_sendBtn->setEnabled(true);
    m_abortBtn->setEnabled(false);
    m_testToolBtn->setEnabled(true);
    m_inputEdit->clear();
}

void LLMConfigWidget::onChunkReceived(const QString& chunk) {
    // 累积文本片段
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
    m_sendBtn->setEnabled(true);
    m_abortBtn->setEnabled(false);
    m_testToolBtn->setEnabled(true);
    m_inputEdit->clear();
    qDebug() << "按钮状态已恢复";
    
    // 更新历史显示
    updateHistoryDisplay();
}

void LLMConfigWidget::onError(const QString& errorMsg) {
    QMessageBox::critical(this, "API 错误", errorMsg);
    onFinished("");
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
    // 注册 create_file 工具
    Tool createFileTool;
    createFileTool.name = "create_file";
    createFileTool.description = "在指定目录创建一个文本文件";
    createFileTool.inputSchema = QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"directory", QJsonObject{
                {"type", "string"},
                {"description", "目标目录路径,例如: E:/test"}
            }},
            {"filename", QJsonObject{
                {"type", "string"},
                {"description", "文件名,例如: hello.txt"}
            }},
            {"content", QJsonObject{
                {"type", "string"},
                {"description", "文件内容,如果未指定则创建空文件"}
            }}
        }},
        {"required", QJsonArray{"directory", "filename"}}  // content 改为可选
    };
    
    m_agent->registerTool(createFileTool);
    qDebug() << "已注册工具: create_file";
    
    // 注册 execute_command 工具
    Tool executeCommandTool;
    executeCommandTool.name = "execute_command";
    executeCommandTool.description = "执行终端命令并返回结果。可以执行 dir, git, qmake, make 等命令";
    executeCommandTool.inputSchema = QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"command", QJsonObject{
                {"type", "string"},
                {"description", "要执行的命令,例如: dir, git status, qmake"}
            }},
            {"working_directory", QJsonObject{
                {"type", "string"},
                {"description", "工作目录 (可选),例如: E:/Document/metagpt_qt-1"}
            }}
        }},
        {"required", QJsonArray{"command"}}
    };
    
    m_agent->registerTool(executeCommandTool);
    qDebug() << "已注册工具: execute_command";
}

void LLMConfigWidget::onTestToolClicked() {
    // 更新 Agent 的角色设定
    m_agent->setSystemPrompt(m_systemPromptEdit->toPlainText().trimmed());
    
    // 清空累积内容
    m_currentAssistantReply.clear();
    
    // 显示测试消息
    m_chatDisplay->append("<br>");
    m_chatDisplay->append("<b style='color: #FF9800;'>🔧 工具调用测试:</b>");
    m_chatDisplay->append("<p>请在 E:/test 目录下创建一个名为 helloworld.txt 的文件,内容是 'Hello from DeepSeek Tool Calling!'</p>");
    m_chatDisplay->append("<b style='color: #4CAF50;'>Assistant:</b>");
    
    m_sendBtn->setEnabled(false);
    m_abortBtn->setEnabled(true);
    m_testToolBtn->setEnabled(false);
    
    // 使用 askWithTools 发起工具调用
    m_agent->askWithTools("请在 E:/test 目录下创建一个名为 helloworld.txt 的文件,内容是 'Hello from DeepSeek Tool Calling!'");
}

void LLMConfigWidget::onToolCallRequested(const QString& toolId, 
                                          const QString& toolName,
                                          const QJsonObject& input) {
    // 显示工具调用信息
    m_chatDisplay->append("<br>");
    m_chatDisplay->append("<b style='color: #9C27B0;'>🔧 工具调用:</b>");
    m_chatDisplay->append(QString("<p>工具: <b>%1</b></p>").arg(toolName));
    m_chatDisplay->append(QString("<p>参数: <code>%1</code></p>")
                         .arg(QString(QJsonDocument(input).toJson(QJsonDocument::Compact))));
    
    QString result;
    
    if (toolName == "create_file") {
        QString directory = input["directory"].toString();
        QString filename = input["filename"].toString();
        QString content = input.value("content").toString();  // 使用 value() 处理可选参数
        
        // 如果没有指定内容,使用默认值
        if (content.isEmpty()) {
            content = "";  // 创建空文件
        }
        
        m_chatDisplay->append(QString("<p>→ 创建文件: %1/%2</p>").arg(directory, filename));
        if (!content.isEmpty()) {
            m_chatDisplay->append(QString("<p>→ 内容: %1</p>").arg(content));
        }
        
        // 执行文件创建
        result = FileTool::createFile(directory, filename, content);
        
        m_chatDisplay->append(QString("<p>→ 结果: %1</p>").arg(result));
    } 
    else if (toolName == "execute_command") {
        QString command = input["command"].toString();
        QString workingDir = input.value("working_directory").toString();
        
        m_chatDisplay->append(QString("<p>→ 执行命令: <code>%1</code></p>")
                             .arg(command.toHtmlEscaped()));
        
        if (!workingDir.isEmpty()) {
            m_chatDisplay->append(QString("<p>→ 工作目录: %1</p>").arg(workingDir));
        }
        
        // 安全检查
        if (!ShellTool::isSafeCommand(command)) {
            result = "错误: 命令被安全策略拒绝 (包含危险操作)";
            m_chatDisplay->append(QString("<p style='color: red;'><b>⚠️ %1</b></p>").arg(result));
        } else {
            // 执行命令
            m_chatDisplay->append("<p>⏳ 正在执行...</p>");
            result = ShellTool::executeCommand(command, workingDir);
            
            // 显示结果 (限制长度)
            QString displayResult = result;
            if (displayResult.length() > 500) {
                displayResult = displayResult.left(500) + "\n...(输出过长,已截断)";
            }
            
            m_chatDisplay->append(QString("<p>→ 结果:</p><pre style='background: #f5f5f5; padding: 10px; border-radius: 5px;'>%1</pre>")
                                 .arg(displayResult.toHtmlEscaped()));
        }
    }
    else {
        result = QString("错误: 未知的工具 %1").arg(toolName);
        m_chatDisplay->append(QString("<p style='color: red;'>→ %1</p>").arg(result));
    }
    
    // 返回结果给 Agent
    m_agent->submitToolResult(toolId, result);
}
