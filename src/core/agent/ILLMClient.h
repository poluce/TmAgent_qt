#ifndef ILLMCLIENT_H
#define ILLMCLIENT_H

#include <QObject>
#include <QJsonArray>
#include "ToolTypes.h"

/**
 * @brief LLM 客户端接口
 * 
 * 职责：具体的网络通信和 API 协议解析（如 DeepSeek, OpenAI）。
 * 它不保留对话历史，只负责单次请求的发送和流式解析。
 */
class ILLMClient : public QObject {
    Q_OBJECT
public:
    explicit ILLMClient(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~ILLMClient() = default;

    /**
     * @brief 发送请求
     * @param config LLM 配置
     * @param messages 消息列表
     * @param tools 可选工具列表
     */
    virtual void postRequest(const LLMConfig& config, 
                            const QJsonArray& messages, 
                            const QList<Tool>& tools = {}) = 0;

    /**
     * @brief 中断请求
     */
    virtual void abort() = 0;

signals:
    /**
     * @brief 收到普通文本片段
     */
    void deltaReceived(const QString& text);

    /**
     * @brief 收到工具调用片段（通常在完成时触发一次完整的工具调用数组）
     * @param toolCalls 标准化的工具调用数组
     */
    void toolCallsReceived(const QJsonArray& toolCalls);

    /**
     * @brief 请求完成
     * @param fullContent 完整的响应内容
     */
    void finished(const QString& fullContent);

    /**
     * @brief 发生错误
     */
    void errorOccurred(const QString& errorMsg);
};

#endif // ILLMCLIENT_H
