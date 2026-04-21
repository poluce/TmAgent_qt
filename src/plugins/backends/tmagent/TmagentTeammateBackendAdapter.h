#ifndef TMAGENTTEAMMATEBACKENDADAPTER_H
#define TMAGENTTEAMMATEBACKENDADAPTER_H

#include <tmagent/plugin/ITeammateBackend.h>
#include <QObject>
#include <QHash>
#include <QPointer>

class LLMAgent;
class Teammate;

/**
 * @brief 适配器类，将 SDK 的 ITeammateBackend 接口桥接到内部实现
 * 
 * 该适配器负责：
 * - 将 SDK 的 TeammateConfig 转换为内部的 Teammate 对象
 * - 管理队友 ID 到 Teammate 对象的映射
 * - 桥接队友会话管理功能
 */
class TmagentTeammateBackendAdapter : public QObject, public TmAgent::ITeammateBackend {
    Q_OBJECT
    
public:
    explicit TmagentTeammateBackendAdapter(QObject* parent = nullptr);
    ~TmagentTeammateBackendAdapter() override;
    
    QString backendId() const override;
    bool ensureReady(QString* error = nullptr) override;
    bool isReady() const override;
    
    CreateResult createSession(const QString& teammateId,
                              const TmAgent::TeammateConfig& config) override;
    SendResult sendMessage(const QString& teammateId,
                          const QString& text) override;
    bool cancelTurn(const QString& teammateId,
                   QString* error = nullptr) override;
    void destroySession(const QString& teammateId) override;
    void shutdown() override;

private:
    struct SessionState {
        QPointer<LLMAgent> agent;
        QString activeTurnId;
        Teammate* teammate = nullptr;  // 临时 Teammate 对象用于内部实现
    };
    
    QString defaultWorkspaceFor(const QString& teammateId, const QString& ownerAgentId) const;
    QStringList allowedToolsForOwner(const QString& ownerAgentId) const;
    
    QHash<QString, SessionState> m_sessions;
};

#endif // TMAGENTTEAMMATEBACKENDADAPTER_H
