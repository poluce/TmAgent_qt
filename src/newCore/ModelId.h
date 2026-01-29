#ifndef MODELID_H
#define MODELID_H

/**
 * @brief 模型枚举类型（公共）
 *
 * 仅表示“模型类别”，不包含具体字符串解析逻辑。
 * 字符串解析与映射由 ModelFactory 负责。
 */
enum class ModelId {
    Unknown,
    DeepSeekChat,
    GPT4o,
    Claude35Sonnet,
    Llama3,
    Gemini15Pro,
    Custom
};

#endif // MODELID_H
