#ifndef LLMPROVIDER_H
#define LLMPROVIDER_H

#include "LLMTypes.h"
#include <QObject>

/**
 * @brief 统一模型调用抽象（设计文档 6.3 LLMProvider）
 *
 * 定位：上层唯一依赖的模型调用接口。
 * 职责：
 *   - 接收“已组装好的请求”（上下文由上层/HistoryManager 完成）
 *   - 提供非流式与流式两套入口
 *   - 返回标准化结果与标准化错误（error_code + user_message）
 * 约束：
 *   - 不拼接历史
 *   - 不直接执行工具
 */
class LLMProvider : public QObject {
    Q_OBJECT
public:
    explicit LLMProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~LLMProvider() override = default;

    /**
     * @brief 非流式生成，一次性返回
     * @param request 已组装的请求（含 messages）
     * @return 标准化响应，失败时 result 为空且 error 已填充
     */
    virtual LLMResponse generate(const LLMRequest& request) = 0;

    /**
     * @brief 流式生成，通过信号逐段返回
     * @param request 已组装的请求，request.stream 应为 true
     *
     * 信号顺序：delta（若干次） -> complete 或 error
     */
    virtual void generateStream(const LLMRequest& request) = 0;

    /**
     * @brief 能力探测：当前 Provider 是否支持给定能力标签
     * @param capability 如 Capability::ToolCalling、Capability::CodeGeneration
     */
    virtual bool supports(const QString& capability) const = 0;

    /**
     * @brief 获取本 Provider 对应的能力描述（只读，用于展示与路由）
     */
    virtual CapabilityDescriptor descriptor() const = 0;

    /**
     * @brief 取消当前正在进行的流式请求（若有）
     */
    virtual void abort() = 0;

signals:
    /// 流式：收到增量文本
    void deltaReceived(const QString& delta);
    /// 流式：收到工具调用（通常在结束时一次性）
    void toolCallsReceived(const QJsonArray& toolCalls);
    /// 流式：本轮生成结束，附带完整文本与用量
    void streamComplete(const QString& fullContent, const LLMUsage& usage);
    /// 流式/非流式：发生错误，统一 error_code + user_message
    void errorOccurred(const LLMError& err);
};

#endif // LLMPROVIDER_H
