#ifndef ITEAMMATEBACKEND_H
#define ITEAMMATEBACKEND_H

#include "core/model/Teammate.h"
#include <QString>

/**
 * @brief 队友后端接口
 *
 * 定义了队友后端必须实现的能力：
 * - 创建会话（Thread）
 * - 发送消息（Turn）
 * - 销毁会话
 *
 * 具体实现：CodexTeammateBackend、未来的 ClaudeCodeTeammateBackend 等。
 */
class ITeammateBackend {
public:
    virtual ~ITeammateBackend() = default;

    struct CreateResult {
        bool success = false;
        QString threadId;
        QString error;
    };

    struct SendResult {
        bool success = false;
        QString turnId;
        QString error;
    };

    /// 后端标识符，如 "codex", "claude-code"
    virtual QString backendId() const = 0;

    /// 确保后端进程/连接就绪
    virtual bool ensureReady(QString* error = nullptr) = 0;

    /// 是否就绪
    virtual bool isReady() const = 0;

    /// 为队友创建一个新的会话（Thread）
    virtual CreateResult createSession(Teammate* mate) = 0;

    /// 向队友发送消息（创建一个 Turn）
    virtual SendResult sendMessage(Teammate* mate, const QString& text) = 0;

    /// 销毁队友的会话
    virtual void destroySession(Teammate* mate) = 0;

    /// 关闭后端
    virtual void shutdown() = 0;
};

#endif // ITEAMMATEBACKEND_H
