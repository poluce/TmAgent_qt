#ifndef TMAGENT_ITEAMMATEBACKEND_H
#define TMAGENT_ITEAMMATEBACKEND_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include "../types/CommonTypes.h"

namespace TmAgent {

/**
 * @brief 队友后端接口
 * 
 * 用于持久化协作场景，支持创建和管理长期存在的队友会话。
 * 队友可以跨多个用户交互保持状态和上下文。
 */
class ITeammateBackend {
public:
    virtual ~ITeammateBackend() = default;
    
    /**
     * @brief 创建会话结果
     */
    struct CreateResult {
        bool success;               ///< 是否成功
        QString threadId;           ///< 会话线程 ID（成功时）
        QString error;              ///< 错误信息（失败时）
        
        CreateResult() : success(false) {}
        CreateResult(bool ok, const QString& tid = QString(), const QString& err = QString())
            : success(ok), threadId(tid), error(err) {}
    };
    
    /**
     * @brief 发送消息结果
     */
    struct SendResult {
        bool success;               ///< 是否成功
        QString turnId;             ///< 回合 ID（成功时）
        QString error;              ///< 错误信息（失败时）
        
        SendResult() : success(false) {}
        SendResult(bool ok, const QString& tid = QString(), const QString& err = QString())
            : success(ok), turnId(tid), error(err) {}
    };
    
    /**
     * @brief 返回后端标识
     * @return QString 后端 ID
     */
    virtual QString backendId() const = 0;
    
    /**
     * @brief 确保后端就绪
     * 
     * 执行必要的初始化操作，确保后端可以创建会话。
     * 
     * @param error 错误信息输出参数（可选）
     * @return true 如果后端就绪
     */
    virtual bool ensureReady(QString* error = nullptr) = 0;
    
    /**
     * @brief 查询后端是否就绪
     * @return true 如果后端已就绪
     */
    virtual bool isReady() const = 0;
    
    /**
     * @brief 创建队友会话
     * 
     * @param teammateId 队友唯一标识符
     * @param config 队友配置
     * @return CreateResult 创建结果，包含线程 ID 或错误信息
     */
    virtual CreateResult createSession(const QString& teammateId,
                                      const TeammateConfig& config) = 0;
    
    /**
     * @brief 发送消息到队友
     * 
     * @param teammateId 队友 ID
     * @param text 消息文本
     * @return SendResult 发送结果，包含回合 ID 或错误信息
     */
    virtual SendResult sendMessage(const QString& teammateId,
                                  const QString& text) = 0;
    
    /**
     * @brief 取消当前回合
     * 
     * @param teammateId 队友 ID
     * @param error 错误信息输出参数（可选）
     * @return true 如果取消成功
     */
    virtual bool cancelTurn(const QString& teammateId,
                           QString* error = nullptr) = 0;
    
    /**
     * @brief 销毁队友会话
     * 
     * 释放与该队友相关的所有资源。
     * 
     * @param teammateId 队友 ID
     */
    virtual void destroySession(const QString& teammateId) = 0;
    
    /**
     * @brief 关闭后端
     * 
     * 销毁所有会话并释放资源。
     */
    virtual void shutdown() = 0;
};

} // namespace TmAgent

#endif // TMAGENT_ITEAMMATEBACKEND_H
