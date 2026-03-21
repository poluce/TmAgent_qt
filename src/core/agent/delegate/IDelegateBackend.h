#ifndef IDELEGATEBACKEND_H
#define IDELEGATEBACKEND_H

#include "core/agent/ToolTypes.h"
#include "llm/LLMTypes.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

class ToolDispatcher;

namespace DelegateBackendInternal {

struct DelegateBackendStartRequest {
    QString task;
    QString executionPrompt;
    LLMConfig childConfig;
    int expectedTimeoutMs = 0;
    int maxResponseChars = 0;
    bool restrictDelegation = false;
    QStringList inheritedAllowedTools;
    ToolDispatcher* toolDispatcher = nullptr;
};

struct DelegateBackendCallbacks {
    std::function<void()> onActivity;
    std::function<void(const QString&)> onSummary;
    std::function<void(const ToolExecutionEvent&)> onToolEvent;
    std::function<void(const QString&)> onStreamDelta;
    std::function<void(const QString&, const QString&, const QString&)> onBackendIdentity;
    std::function<void(const QString&, const QString&, const QJsonObject&)> onTimelineEvent;
    std::function<void(const QString&)> onSuccess;
    std::function<void(const QString&)> onFailure;
};

class IDelegateBackendSession {
public:
    virtual ~IDelegateBackendSession() = default;

    virtual QString backendId() const = 0;
    virtual QString backendProgram() const = 0;
    virtual void start() = 0;
    virtual void cancel() = 0;
};

class IDelegateBackend {
public:
    virtual ~IDelegateBackend() = default;

    virtual QString backendId() const = 0;
    virtual std::unique_ptr<IDelegateBackendSession> createSession(
        const DelegateBackendStartRequest& request,
        const DelegateBackendCallbacks& callbacks,
        QString* error) = 0;
};

} // namespace DelegateBackendInternal

#endif // IDELEGATEBACKEND_H
