#include "ToolLogWidget.h"
#include "../core/agent/AgentEventBus.h"
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

ToolLogWidget::ToolLogWidget(QWidget* parent) : QWidget(parent)
{
    setupUI();
    setWindowTitle("工具执行日志 - RAW Data");
    resize(800, 600);

    // NOTE: 自主订阅全局事件总线
    connect(AgentEventBus::instance(), &AgentEventBus::toolEventReceived, this, &ToolLogWidget::logEvent);
}

void ToolLogWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    // 顶部工具栏
    QHBoxLayout* topLayout = new QHBoxLayout();
    QLabel* titleLabel = new QLabel("<b>工具执行详细流水</b>", this);
    m_clearBtn = new QPushButton("清空日志", this);
    m_clearBtn->setStyleSheet("background-color: #2d2f33; color: #f3f4f6; border: 1px solid #3c3c3c; border-radius: 10px; padding: 6px 10px;");

    topLayout->addWidget(titleLabel);
    topLayout->addStretch();
    topLayout->addWidget(m_clearBtn);

    layout->addLayout(topLayout);

    // 日志显示区
    m_logDisplay = new QTextBrowser(this);
    m_logDisplay->setReadOnly(true);
    m_logDisplay->setUndoRedoEnabled(false);

    // 设置深色/代码风格背景
    m_logDisplay->setStyleSheet("background-color: #1e1e1e; color: #d4d4d4; border: 1px solid #3c3c3c; border-radius: 12px; padding: 6px; font-family: 'Consolas', 'Monaco', monospace;");

    layout->addWidget(m_logDisplay);

    connect(m_clearBtn, &QPushButton::clicked, this, &ToolLogWidget::clearLogs);
}

namespace {
QString statusColor(const ToolExecutionEvent& event)
{
    if (event.status == QLatin1String("started"))
        return QStringLiteral("#569cd6");
    if (event.status == QLatin1String("progress"))
        return QStringLiteral("#d7ba7d");
    if (event.status == QLatin1String("completed"))
        return event.success ? QStringLiteral("#6a9955") : QStringLiteral("#f44747");
    return QStringLiteral("#d4d4d4");
}

QString contentSectionHtml(const QString& labelColor, const QString& label, const QString& body)
{
    static const QString kPreStyle = QStringLiteral("background: #1e1e1e; padding: 12px; border: 1px solid #3c3c3c; "
                                                    "border-radius: 10px; margin-top: 6px; white-space: pre-wrap; "
                                                    "color: #cccccc; font-family: Consolas; font-size: 12px;");
    return QStringLiteral(
               "<div style='margin-top: 12px; border-top: 1px solid #333; padding-top: 8px;'>"
               "<span style='color: %1; font-size: 12px;'>%2</span>"
               "<pre style='%3'>%4</pre></div>")
        .arg(labelColor, label, kPreStyle, body.toHtmlEscaped());
}
} // namespace

void ToolLogWidget::logEvent(const ToolExecutionEvent& event)
{
    const QString timeStr = formatTimestamp();
    const QString color = statusColor(event);
    const QString statusText = event.status.toUpper();

    QString html;

    if (event.status == QLatin1String("started"))
        html += QStringLiteral("<div style='margin-top: 30px; margin-bottom: 5px; height: 1px;'></div>");

    html += QStringLiteral(
        "<table width='100%' cellspacing='0' cellpadding='0' "
        "style='margin-bottom: 12px; border: 1px solid #3c3c3c; border-radius: 12px; "
        "background-color: #252526;'><tr><td style='padding: 12px;'>");

    html += QStringLiteral("<table width='100%'><tr>");
    html += QStringLiteral("<td><span style='color: #4ec9b0; font-weight: bold; font-size: 14px;'>%1</span> "
                           "<span style='color: #808080; font-size: 11px; margin-left: 8px;'>ID: %2</span></td>")
                .arg(event.toolName, event.toolId);
    html += QStringLiteral("<td align='right'>"
                           "<span style='background-color: %1; color: #ffffff; padding: 2px 8px; "
                           "font-weight: bold; font-size: 10px; border-radius: 8px;'>%2</span> "
                           "<span style='color: #808080; font-size: 11px; margin-left: 10px;'>%3</span>"
                           "</td></tr></table>")
                .arg(color, statusText, timeStr);

    if (event.status == QLatin1String("started")) {
        const QString params = QJsonDocument(event.data).toJson(QJsonDocument::Indented);
        html += contentSectionHtml(QStringLiteral("#569cd6"), QStringLiteral("&#9660; <b>INPUT PARAMETERS</b>"), params);
    } else if (event.status == QLatin1String("progress")) {
        html += contentSectionHtml(QStringLiteral("#d7ba7d"), QStringLiteral("&#9203; <b>PROGRESS</b>"), event.formattedResult);
    } else if (event.status == QLatin1String("completed")) {
        const QString resultColor = event.success ? QStringLiteral("#6a9955") : QStringLiteral("#f44747");
        html += contentSectionHtml(resultColor, QStringLiteral("&#9650; <b>OUTPUT RESULT</b>"), event.rawResult);
    }

    html += QStringLiteral("</td></tr></table>");

    if (event.status == QLatin1String("completed"))
        html += QStringLiteral("<div style='margin-bottom: 20px;'></div>");

    m_logDisplay->append(html);
    m_logDisplay->ensureCursorVisible();
}

void ToolLogWidget::clearLogs()
{
    m_logDisplay->clear();
}

QString ToolLogWidget::formatTimestamp()
{
    return QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
}
