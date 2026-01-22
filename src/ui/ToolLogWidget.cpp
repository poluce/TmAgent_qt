#include "ToolLogWidget.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include "../core/agent/AgentEventBus.h"

ToolLogWidget::ToolLogWidget(QWidget *parent) : QWidget(parent) {
    setupUI();
    setWindowTitle("工具执行日志 - RAW Data");
    resize(800, 600);
    
    // NOTE: 自主订阅全局事件总线
    connect(AgentEventBus::instance(), &AgentEventBus::toolEventReceived, 
            this, &ToolLogWidget::logEvent);
}

void ToolLogWidget::setupUI() {
    QVBoxLayout *layout = new QVBoxLayout(this);
    
    // 顶部工具栏
    QHBoxLayout *topLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel("<b>工具执行详细流水</b>", this);
    m_clearBtn = new QPushButton("清空日志", this);
    
    topLayout->addWidget(titleLabel);
    topLayout->addStretch();
    topLayout->addWidget(m_clearBtn);
    
    layout->addLayout(topLayout);
    
    // 日志显示区
    m_logDisplay = new QTextBrowser(this);
    m_logDisplay->setReadOnly(true);
    m_logDisplay->setUndoRedoEnabled(false);
    
    // 设置深色/代码风格背景
    m_logDisplay->setStyleSheet("background-color: #1e1e1e; color: #d4d4d4; font-family: 'Consolas', 'Monaco', monospace;");
    
    layout->addWidget(m_logDisplay);
    
    connect(m_clearBtn, &QPushButton::clicked, this, &ToolLogWidget::clearLogs);
}

void ToolLogWidget::logEvent(const ToolExecutionEvent& event) {
    QString timeStr = formatTimestamp();
    QString color = "#d4d4d4";
    QString statusText = event.status.toUpper();
    
    if (event.status == "started") {
        color = "#569cd6"; // 蓝色
    } else if (event.status == "completed") {
        color = event.success ? "#6a9955" : "#f44747"; // 绿色或红色
    }
    
    QString html;
    
    // 如果是开始执行，添加一个明显的顶部分隔空间
    if (event.status == "started") {
        html += "<div style='margin-top: 30px; margin-bottom: 5px; height: 1px;'></div>";
    }

    // 使用表格(table)来确保在 Qt 的 QTextBrowser 中有稳固的边框感
    // border-left 稍微加宽，强调状态
    html += QString("<table width='100%' cellspacing='0' cellpadding='0' style='margin-bottom: 12px; border: 1px solid #3c3c3c; background-color: #252526;'>");
    html += "<tr><td style='padding: 12px;'>";

    // 头部区域：采用两列布局（模拟）
    html += "<table width='100%'><tr>";
    // 左侧：名称和 ID
    html += QString("<td><span style='color: #4ec9b0; font-weight: bold; font-size: 14px;'>%1</span> ").arg(event.toolName);
    html += QString("<span style='color: #808080; font-size: 11px; margin-left: 8px;'>ID: %1</span></td>").arg(event.toolId);
    // 右侧：状态和时间
    html += "<td align='right'>";
    html += QString("<span style='background-color: %1; color: #ffffff; padding: 2px 8px; font-weight: bold; font-size: 10px; border-radius: 2px;'>%2</span> ").arg(color, statusText);
    html += QString("<span style='color: #808080; font-size: 11px; margin-left: 10px;'>%1</span>").arg(timeStr);
    html += "</td></tr></table>";
    
    // 内容区域
    if (event.status == "started") {
        QString params = QJsonDocument(event.data).toJson(QJsonDocument::Indented);
        html += "<div style='margin-top: 12px; border-top: 1px solid #333; padding-top: 8px;'>";
        html += "<span style='color: #569cd6; font-size: 12px;'>▼ <b>INPUT PARAMETERS</b></span>";
        html += QString("<pre style='background: #1e1e1e; padding: 12px; border: 1px solid #3c3c3c; margin-top: 6px; color: #9cdcfe; font-family: Consolas; font-size: 12px;'>%1</pre>").arg(params.toHtmlEscaped());
        html += "</div>";
    } else if (event.status == "completed") {
        html += "<div style='margin-top: 12px; border-top: 1px solid #333; padding-top: 8px;'>";
        html += QString("<span style='color: %1; font-size: 12px;'>▲ <b>OUTPUT RESULT</b></span>").arg(event.success ? "#6a9955" : "#f44747");
        html += QString("<pre style='background: #1e1e1e; padding: 12px; border: 1px solid #3c3c3c; margin-top: 6px; white-space: pre-wrap; color: #cccccc; font-family: Consolas; font-size: 12px;'>%1</pre>").arg(event.rawResult.toHtmlEscaped());
        html += "</div>";
    }
    
    html += "</td></tr></table>";

    // 状态结束后的补空
    if (event.status == "completed") {
        html += "<div style='margin-bottom: 20px;'></div>";
    }
    
    m_logDisplay->append(html);
    m_logDisplay->ensureCursorVisible();
}

void ToolLogWidget::clearLogs() {
    m_logDisplay->clear();
}

QString ToolLogWidget::formatTimestamp() {
    return QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
}
