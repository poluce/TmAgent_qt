#include "ToolLogWidget.h"
#include "HistoryFormatters.h"
#include "../core/agent/AgentEventBus.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// 匿名命名空间：HTML 辅助函数
// ---------------------------------------------------------------------------
namespace {

QString clampLogBody(const QString& body, int maxChars = 4000)
{
    if (body.size() <= maxChars)
        return body;
    return body.left(maxChars)
        + QStringLiteral("\n\n...[日志内容过长，已截断，共 %1 字符]...").arg(body.size());
}

QString statusColor(const ToolExecutionEvent& event)
{
    if (event.status == QLatin1String("started"))
        return QStringLiteral("#569cd6");
    if (event.status == QLatin1String("progress"))
        return QStringLiteral("#d7ba7d");
    if (event.status == QLatin1String("completed"))
        return event.success ? QStringLiteral("#6a9955") : QStringLiteral("#f44747");
    if (event.status == QLatin1String("error"))
        return QStringLiteral("#f44747");
    return QStringLiteral("#d4d4d4");
}

QString contentSectionHtml(const QString& labelColor, const QString& label, const QString& body)
{
    static const QString kPreStyle = QStringLiteral(
        "background: #1e1e1e; padding: 12px; border: 1px solid #3c3c3c; "
        "border-radius: 10px; margin-top: 6px; white-space: pre-wrap; "
        "color: #cccccc; font-family: Consolas; font-size: 12px;");
    return QStringLiteral(
               "<div style='margin-top: 12px; border-top: 1px solid #333; padding-top: 8px;'>"
               "<span style='color: %1; font-size: 12px;'>%2</span>"
               "<pre style='%3'>%4</pre></div>")
        .arg(labelColor, label, kPreStyle, body.toHtmlEscaped());
}

} // namespace

// ---------------------------------------------------------------------------
// 构造函数
// ---------------------------------------------------------------------------
ToolLogWidget::ToolLogWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setWindowTitle(HistoryFormatters::toolLogWindowTitle());
    resize(800, 600);

    connect(AgentEventBus::instance(), &AgentEventBus::toolEventReceived,
            this, &ToolLogWidget::logEvent);
}

// ---------------------------------------------------------------------------
// setupUI
// ---------------------------------------------------------------------------
void ToolLogWidget::setupUI()
{
    auto* layout = new QVBoxLayout(this);

    // --- 顶部工具栏 ---
    auto* topLayout = new QHBoxLayout();

    // 搜索框
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("搜索关键词...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setStyleSheet(
        "background-color: #2d2f33; color: #f3f4f6; border: 1px solid #3c3c3c; "
        "border-radius: 10px; padding: 6px 10px;");

    // Ctrl+F 快捷键聚焦搜索框
    auto* searchShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(searchShortcut, &QShortcut::activated, m_searchEdit, [this]() {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });

    // 工具名过滤
    m_toolNameFilter = new QComboBox(this);
    m_toolNameFilter->addItem("全部工具");
    m_toolNameFilter->setMinimumWidth(120);
    m_toolNameFilter->setStyleSheet(
        "QComboBox { background-color: #2d2f33; color: #f3f4f6; border: 1px solid #3c3c3c; "
        "border-radius: 10px; padding: 4px 8px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #2d2f33; color: #f3f4f6; "
        "selection-background-color: #3c3c3c; }");

    // 状态过滤
    m_statusFilter = new QComboBox(this);
    m_statusFilter->addItems({"全部状态", "started", "progress", "completed", "error"});
    m_statusFilter->setMinimumWidth(100);
    m_statusFilter->setStyleSheet(m_toolNameFilter->styleSheet());

    // 暂停/恢复按钮
    m_pauseBtn = new QPushButton("暂停", this);
    m_pauseBtn->setStyleSheet(
        "background-color: #2d2f33; color: #f3f4f6; border: 1px solid #3c3c3c; "
        "border-radius: 10px; padding: 6px 10px;");

    // 清空按钮
    m_clearBtn = new QPushButton("清空日志", this);
    m_clearBtn->setStyleSheet(m_pauseBtn->styleSheet());

    topLayout->addWidget(m_searchEdit, 1);
    topLayout->addWidget(m_toolNameFilter);
    topLayout->addWidget(m_statusFilter);
    topLayout->addWidget(m_pauseBtn);
    topLayout->addWidget(m_clearBtn);

    layout->addLayout(topLayout);

    // --- 日志显示区 ---
    m_logDisplay = new QTextBrowser(this);
    m_logDisplay->setReadOnly(true);
    m_logDisplay->setUndoRedoEnabled(false);
    m_logDisplay->setOpenExternalLinks(false);
    m_logDisplay->setStyleSheet(
        "background-color: #1e1e1e; color: #d4d4d4; border: 1px solid #3c3c3c; "
        "border-radius: 12px; padding: 6px; font-family: 'Consolas', 'Monaco', monospace;");

    // 右键菜单
    m_logDisplay->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_logDisplay, &QWidget::customContextMenuRequested,
            this, &ToolLogWidget::onContextMenu);

    layout->addWidget(m_logDisplay);

    // --- 信号连接 ---
    connect(m_clearBtn, &QPushButton::clicked, this, &ToolLogWidget::clearLogs);
    connect(m_pauseBtn, &QPushButton::clicked, this, &ToolLogWidget::onTogglePause);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ToolLogWidget::onSearchTextChanged);
    connect(m_toolNameFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolLogWidget::onFilterChanged);
    connect(m_statusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolLogWidget::onFilterChanged);
}

// ---------------------------------------------------------------------------
// logEvent — 核心入口
// ---------------------------------------------------------------------------
void ToolLogWidget::logEvent(const ToolExecutionEvent& event)
{
    if (m_paused) {
        m_pendingEvents.append(event);
        return;
    }

    // 容量控制
    if (m_allEvents.size() >= kMaxEvents)
        m_allEvents.removeFirst();
    m_allEvents.append(event);

    // 动态收集工具名
    if (!m_knownToolNames.contains(event.toolName)) {
        m_knownToolNames.insert(event.toolName);
        m_toolNameFilter->addItem(event.toolName);
    }

    // 仅在匹配过滤条件时追加显示
    if (matchesFilter(event))
        appendEventHtml(event);
}

// ---------------------------------------------------------------------------
// clearLogs
// ---------------------------------------------------------------------------
void ToolLogWidget::clearLogs()
{
    m_allEvents.clear();
    m_pendingEvents.clear();
    m_knownToolNames.clear();
    m_logDisplay->clear();

    // 重置工具名过滤下拉
    m_toolNameFilter->clear();
    m_toolNameFilter->addItem("全部工具");
}

// ---------------------------------------------------------------------------
// onTogglePause
// ---------------------------------------------------------------------------
void ToolLogWidget::onTogglePause()
{
    m_paused = !m_paused;
    m_pauseBtn->setText(m_paused ? "恢复" : "暂停");

    if (!m_paused && !m_pendingEvents.isEmpty()) {
        for (const auto& ev : m_pendingEvents) {
            if (m_allEvents.size() >= kMaxEvents)
                m_allEvents.removeFirst();
            m_allEvents.append(ev);

            if (!m_knownToolNames.contains(ev.toolName)) {
                m_knownToolNames.insert(ev.toolName);
                m_toolNameFilter->addItem(ev.toolName);
            }

            if (matchesFilter(ev))
                appendEventHtml(ev);
        }
        m_pendingEvents.clear();
    }
}

// ---------------------------------------------------------------------------
// onFilterChanged / onSearchTextChanged
// ---------------------------------------------------------------------------
void ToolLogWidget::onFilterChanged()
{
    rebuildDisplay();
}

void ToolLogWidget::onSearchTextChanged(const QString& /*text*/)
{
    rebuildDisplay();
}

// ---------------------------------------------------------------------------
// rebuildDisplay — 根据当前过滤条件重新渲染
// ---------------------------------------------------------------------------
void ToolLogWidget::rebuildDisplay()
{
    m_logDisplay->clear();
    for (const auto& ev : m_allEvents) {
        if (matchesFilter(ev))
            appendEventHtml(ev);
    }
}

// ---------------------------------------------------------------------------
// matchesFilter
// ---------------------------------------------------------------------------
bool ToolLogWidget::matchesFilter(const ToolExecutionEvent& event) const
{
    // 工具名过滤
    const QString selectedTool = m_toolNameFilter->currentText();
    if (selectedTool != "全部工具" && event.toolName != selectedTool)
        return false;

    // 状态过滤
    const QString selectedStatus = m_statusFilter->currentText();
    if (selectedStatus != "全部状态" && event.status != selectedStatus)
        return false;

    // 搜索关键词
    const QString keyword = m_searchEdit->text().trimmed();
    if (!keyword.isEmpty()) {
        const bool found = event.toolName.contains(keyword, Qt::CaseInsensitive)
            || event.toolId.contains(keyword, Qt::CaseInsensitive)
            || event.rawResult.contains(keyword, Qt::CaseInsensitive)
            || event.formattedResult.contains(keyword, Qt::CaseInsensitive);
        if (!found)
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// buildEventHtml — 生成单条事件的 HTML 卡片
// ---------------------------------------------------------------------------
QString ToolLogWidget::buildEventHtml(const ToolExecutionEvent& event) const
{
    const QString timeStr = formatTimestamp();
    const QString color = statusColor(event);
    const QString statusText = event.status.toUpper();

    QString html;

    if (event.status == QLatin1String("started"))
        html += QStringLiteral("<div style='margin-top: 30px; margin-bottom: 5px; height: 1px;'></div>");

    // 卡片外框
    html += QStringLiteral(
        "<table width='100%' cellspacing='0' cellpadding='0' "
        "style='margin-bottom: 12px; border: 1px solid #3c3c3c; border-radius: 12px; "
        "background-color: #252526;'><tr><td style='padding: 12px;'>");

    // 标题行：工具名 + ID | 状态 + 时间
    html += QStringLiteral("<table width='100%'><tr>");
    html += QStringLiteral(
                "<td><span style='color: #4ec9b0; font-weight: bold; font-size: 14px;'>%1</span> "
                "<span style='color: #808080; font-size: 11px; margin-left: 8px;'>ID: %2</span></td>")
                .arg(event.toolName, event.toolId);
    html += QStringLiteral(
                "<td align='right'>"
                "<span style='background-color: %1; color: #ffffff; padding: 2px 8px; "
                "font-weight: bold; font-size: 10px; border-radius: 8px;'>%2</span> "
                "<span style='color: #808080; font-size: 11px; margin-left: 10px;'>%3</span>"
                "</td></tr></table>")
                .arg(color, statusText, timeStr);

    // 内容区
    if (event.status == QLatin1String("started")) {
        const QString params = clampLogBody(
            QString::fromUtf8(QJsonDocument(event.data).toJson(QJsonDocument::Indented)));
        html += contentSectionHtml(
            QStringLiteral("#569cd6"), QStringLiteral("&#9660; <b>INPUT PARAMETERS</b>"), params);
    } else if (event.status == QLatin1String("progress")) {
        html += contentSectionHtml(
            QStringLiteral("#d7ba7d"), QStringLiteral("&#9203; <b>PROGRESS</b>"),
            clampLogBody(event.formattedResult, 800));
    } else if (event.status == QLatin1String("completed")) {
        const QString resultColor = event.success ? QStringLiteral("#6a9955") : QStringLiteral("#f44747");
        html += contentSectionHtml(
            resultColor, QStringLiteral("&#9650; <b>OUTPUT RESULT</b>"),
            clampLogBody(event.rawResult));
    } else if (event.status == QLatin1String("error")) {
        html += contentSectionHtml(
            QStringLiteral("#f44747"), QStringLiteral("&#10060; <b>ERROR</b>"),
            clampLogBody(event.rawResult));
    }

    html += QStringLiteral("</td></tr></table>");

    if (event.status == QLatin1String("completed") || event.status == QLatin1String("error"))
        html += QStringLiteral("<div style='margin-bottom: 20px;'></div>");

    return html;
}

// ---------------------------------------------------------------------------
// appendEventHtml
// ---------------------------------------------------------------------------
void ToolLogWidget::appendEventHtml(const ToolExecutionEvent& event)
{
    m_logDisplay->append(buildEventHtml(event));
    m_logDisplay->ensureCursorVisible();
}

// ---------------------------------------------------------------------------
// formatTimestamp
// ---------------------------------------------------------------------------
QString ToolLogWidget::formatTimestamp() const
{
    return QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
}

// ---------------------------------------------------------------------------
// onContextMenu — 右键菜单
// ---------------------------------------------------------------------------
void ToolLogWidget::onContextMenu(const QPoint& pos)
{
    // 尝试从光标附近的 HTML 中提取 tool ID
    QTextCursor cursor = m_logDisplay->cursorForPosition(pos);
    cursor.select(QTextCursor::BlockUnderCursor);
    const QString blockText = cursor.selectedText();

    // 从 "ID: xxx" 模式中提取 toolId
    QString toolId;
    const int idIdx = blockText.indexOf("ID: ");
    if (idIdx >= 0) {
        const int start = idIdx + 4;
        int end = start;
        while (end < blockText.size() && !blockText[end].isSpace())
            ++end;
        toolId = blockText.mid(start, end - start);
    }

    // 如果没有从当前行找到，尝试从所有可见文本中查找最近的 ID
    if (toolId.isEmpty()) {
        // 向上搜索包含 ID 的行
        QTextCursor searchCursor = cursor;
        for (int i = 0; i < 20 && searchCursor.movePosition(QTextCursor::Up); ++i) {
            searchCursor.select(QTextCursor::BlockUnderCursor);
            const QString line = searchCursor.selectedText();
            const int idx = line.indexOf("ID: ");
            if (idx >= 0) {
                const int s = idx + 4;
                int e = s;
                while (e < line.size() && !line[e].isSpace())
                    ++e;
                toolId = line.mid(s, e - s);
                break;
            }
        }
    }

    QMenu menu(this);

    if (!toolId.isEmpty()) {
        QAction* copyCliAction = menu.addAction("复制 CLI 查询命令");
        connect(copyCliAction, &QAction::triggered, this, [toolId]() {
            const QString cmd = QString("tmagent-log search --tool-call-id %1").arg(toolId);
            QApplication::clipboard()->setText(cmd);
        });

        QAction* copyIdAction = menu.addAction("复制 Tool Call ID");
        connect(copyIdAction, &QAction::triggered, this, [toolId]() {
            QApplication::clipboard()->setText(toolId);
        });

        menu.addSeparator();
    }

    QAction* copyAction = menu.addAction("复制选中文本");
    connect(copyAction, &QAction::triggered, m_logDisplay, &QTextBrowser::copy);

    menu.exec(m_logDisplay->mapToGlobal(pos));
}
