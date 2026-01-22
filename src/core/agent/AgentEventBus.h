#ifndef AGENTEVENTBUS_H
#define AGENTEVENTBUS_H

#include <QObject>
#include <QString>
#include "ToolTypes.h"

/**
 * @brief 全局事件总线 (单例)
 * 
 * 职责:
 *  - 作为一个全局的信号中转站，解耦事件发送方（Agent/工具）和接收方（UI/日志）。
 *  - 支持任何组件发送 ToolExecutionEvent。
 *  - 未来可扩展支持更多类型的全局信号。
 */
class AgentEventBus : public QObject {
    Q_OBJECT
public:
    static AgentEventBus* instance() {
        static AgentEventBus bus;
        return &bus;
    }

    /**
     * @brief 分发工具事件
     * @param event 工具执行事件
     */
    void postToolEvent(const ToolExecutionEvent& event) {
        emit toolEventReceived(event);
    }

    /**
     * @brief 分发通用日志信息
     * @param message 日志文本
     * @param level 日志级别 (info, warning, error)
     */
    void postLog(const QString& message, const QString& level = "info") {
        emit logReceived(message, level);
    }

signals:
    /// 当收到任何工具事件时发射
    void toolEventReceived(const ToolExecutionEvent& event);
    
    /// 当收到通用日志信息时发射
    void logReceived(const QString& message, const QString& level);

private:
    explicit AgentEventBus(QObject *parent = nullptr) : QObject(parent) {}
    ~AgentEventBus() = default;
    
    // 禁止拷贝
    AgentEventBus(const AgentEventBus&) = delete;
    AgentEventBus& operator=(const AgentEventBus&) = delete;
};

#endif // AGENTEVENTBUS_H
