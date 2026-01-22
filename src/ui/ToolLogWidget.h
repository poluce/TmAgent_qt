#ifndef TOOLLOGWIDGET_H
#define TOOLLOGWIDGET_H

#include <QWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>
#include "../core/agent/ToolTypes.h"

/**
 * @brief 工具日志独立展示窗口
 * 
 * 职责:
 *  - 展示工具调用的详细入参
 *  - 展示执行后的原样结果（RAW Output）
 *  - 提供清晰的时间戳和状态标识
 */
class ToolLogWidget : public QWidget {
    Q_OBJECT
public:
    explicit ToolLogWidget(QWidget *parent = nullptr);

public slots:
    /**
     * @brief 记录工具事件
     * @param event 工具执行事件
     */
    void logEvent(const ToolExecutionEvent& event);
    
    /**
     * @brief 清空日志
     */
    void clearLogs();

private:
    void setupUI();
    QString formatTimestamp();

    QTextBrowser *m_logDisplay;
    QPushButton *m_clearBtn;
};

#endif // TOOLLOGWIDGET_H
