#ifndef TMAGENTPLUGIN_TMAGENTTEAMMATEBACKEND_H
#define TMAGENTPLUGIN_TMAGENTTEAMMATEBACKEND_H

#include "core/service/include/ITeammateBackend.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>

class LLMAgent;

class TmagentTeammateBackend : public QObject, public ITeammateBackend {
    Q_OBJECT
public:
    explicit TmagentTeammateBackend(QObject* parent = nullptr);
    ~TmagentTeammateBackend() override;

    QString backendId() const override { return QStringLiteral("tmagent"); }
    bool ensureReady(QString* error = nullptr) override;
    bool isReady() const override;
    CreateResult createSession(Teammate* mate) override;
    SendResult sendMessage(Teammate* mate, const QString& text) override;
    bool cancelTurn(Teammate* mate, QString* error = nullptr) override;
    void destroySession(Teammate* mate) override;
    void shutdown() override;

private:
    struct SessionState {
        QPointer<LLMAgent> agent;
        QString activeTurnId;
    };

    QString defaultWorkspaceFor(const Teammate* mate) const;
    QStringList allowedToolsForOwner(const QString& ownerAgentId) const;

    QHash<QString, SessionState> m_sessions;
};

#endif // TMAGENTPLUGIN_TMAGENTTEAMMATEBACKEND_H
