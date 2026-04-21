#ifndef TMAGENTDELEGATEBACKENDADAPTER_H
#define TMAGENTDELEGATEBACKENDADAPTER_H

#include <tmagent/plugin/IDelegateBackend.h>
#include <QObject>

/**
 * @brief 适配器类，将 SDK 的 IDelegateBackend 接口桥接到内部实现
 * 
 * 该适配器负责：
 * - 将 SDK 的 DelegateRequest 转换为内部的 DelegateBackendStartRequest
 * - 将 IToolExecutor 和 IModelFactory 接口桥接到 ToolDispatcher 和 ModelFactory
 * - 管理委托会话的生命周期
 */
class TmagentDelegateBackendAdapter : public QObject, public TmAgent::IDelegateBackend {
    Q_OBJECT
    
public:
    explicit TmagentDelegateBackendAdapter(QObject* parent = nullptr);
    ~TmagentDelegateBackendAdapter() override = default;
    
    QString backendId() const override;
    
    std::unique_ptr<TmAgent::IDelegateSession> createSession(
        const TmAgent::DelegateRequest& request,
        const TmAgent::DelegateCallbacks& callbacks,
        QString* error = nullptr) override;
};

#endif // TMAGENTDELEGATEBACKENDADAPTER_H
