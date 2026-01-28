#ifndef LLMTYPES_H
#define LLMTYPES_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

/**
 * @file LLMTypes.h
 * @brief 多模型支持体系中的请求/响应、能力描述、路由等数据类型
 *
 * 与《Agent 多模型支持设计》中的 LLMRequest、LLMResponse、CapabilityDescriptor、
 * 以及 ModelRouter 的输入输出保持一致。
 */

// -----------------------------------------------------------------------------
// 能力标签（设计文档 5.1）
// -----------------------------------------------------------------------------
namespace Capability {
inline const QString TextGeneration = QStringLiteral("text_generation");
inline const QString StructuredOutput = QStringLiteral("structured_output");
inline const QString CodeGeneration = QStringLiteral("code_generation");
inline const QString ToolCalling = QStringLiteral("tool_calling");
inline const QString Multimodal = QStringLiteral("multimodal");
} // namespace Capability

// -----------------------------------------------------------------------------
// 错误码（设计文档 9.6.5 Error Taxonomy）
// -----------------------------------------------------------------------------
namespace LLMErrorCode {
inline const QString Timeout = QStringLiteral("timeout");
inline const QString RateLimited = QStringLiteral("rate_limited");
inline const QString AuthFailed = QStringLiteral("auth_failed");
inline const QString ProtocolError = QStringLiteral("protocol_error");
inline const QString ModelRefusal = QStringLiteral("model_refusal");
inline const QString ToolFailed = QStringLiteral("tool_failed");
inline const QString Unknown = QStringLiteral("unknown");
} // namespace LLMErrorCode

// -----------------------------------------------------------------------------
// CapabilityDescriptor：运行时描述模型能力（设计文档 6.6）
// -----------------------------------------------------------------------------
struct CapabilityDescriptor {
    QString modelId;
    QStringList capabilities;  // 能力标签：text_generation, tool_calling 等
    int contextLength = 0;
    bool toolCalling = false;
    QString codeQuality;   // 如 "high" / "medium" / "low" 或空
    QString costLevel;     // 如 "low" / "medium" / "high"
    QString latency;       // 如 "low" / "medium" / "high"
    bool isDefault = false;

    bool supports(const QString& capability) const {
        return capabilities.contains(capability);
    }
};

// -----------------------------------------------------------------------------
// LLM 用量信息
// -----------------------------------------------------------------------------
struct LLMUsage {
    int promptTokens = 0;
    int completionTokens = 0;
    int totalTokens = 0;
};

// -----------------------------------------------------------------------------
// LLM 错误信息（Provider 统一产出 error_code + user_message）
// -----------------------------------------------------------------------------
struct LLMError {
    QString errorCode;
    QString userMessage;
    QJsonObject diagnostics;  // Adapter 提供的诊断信息（HTTP 状态、SSE 等）
};

// -----------------------------------------------------------------------------
// LLMRequest：Provider 接收的已组装请求（设计文档 6.3）
// -----------------------------------------------------------------------------
struct LLMRequest {
    QString requestId;
    QString traceId;
    QString modelId;
    QStringList capabilities;  // 本任务需要的能力标签
    bool stream = false;
    QJsonArray messages;       // 由上层/HistoryManager 完成拼接
    QJsonArray tools;          // 本次请求可用工具（OpenAI 兼容 function 数组）
    int timeoutMs = 180000;
    double temperature = 0.7;
    int maxTokens = 4096;
};

// -----------------------------------------------------------------------------
// LLMResponse：非流式调用的标准化结果（设计文档 6.3）
// -----------------------------------------------------------------------------
struct LLMResponse {
    QString result;            // 完整文本结果
    LLMUsage usage;
    LLMError error;            // 若失败则填充，result 可为空
    QJsonArray toolCalls;      // 若存在工具调用则填充
    QString finishReason;      // "stop" / "tool_calls" / "length" 等

    bool hasError() const { return !error.errorCode.isEmpty(); }
};

// -----------------------------------------------------------------------------
// ModelRouter 输入（设计文档 6.2）
// -----------------------------------------------------------------------------
struct RouterRequest {
    QString taskType;          // 任务类型，供策略使用
    QStringList requiredCapabilities;
    QString costPreference;    // "minimize" / "balance" / "quality"
    int maxLatencyMs = -1;     // -1 表示不限制
    QString preferredModelId;  // 可选偏好
};

// -----------------------------------------------------------------------------
// ModelRouter 输出（设计文档 6.2）
// -----------------------------------------------------------------------------
struct RouterResult {
    QString modelId;
    QString decisionReason;
    QStringList fallbackChain; // 主选失败时的备用 model_id 顺序
    bool success = false;
};

#endif // LLMTYPES_H
