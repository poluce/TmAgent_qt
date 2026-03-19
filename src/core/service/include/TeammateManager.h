#ifndef TEAMMATEMANAGER_H
#define TEAMMATEMANAGER_H

#include "ITeammateBackend.h"
#include "core/model/Teammate.h"
#include <QHash>
#include <QObject>
#include <QString>

/**
 * @brief 通用队友管理器
 *
 * 管理多个后端（Codex、Claude Code 等）和多个持久化队友。
 * 工具层通过此类统一操作，不感知具体后端实现。
 */
class TeammateManager : public QObject {
    Q_OBJECT
public:
    static TeammateManager* instance();

    struct CreateResult {
        bool success = false;
        QString teammateId;
        QString threadId;
        QString error;
    };

    struct MessageResult {
        bool success = false;
        QString turnId;
        QString error;
    };

    // ── 后端注册 ──
    void registerBackend(ITeammateBackend* backend);
    ITeammateBackend* backend(const QString& backendId) const;
    QStringList registeredBackendIds() const;

    // ── 队友生命周期 ──
    CreateResult createTeammate(const Teammate::Config& config);
    bool removeTeammate(const QString& teammateId, QString* error = nullptr);

    // ── 对话 ──
    MessageResult sendMessage(const QString& teammateId, const QString& text);

    // ── 查询 ──
    Teammate* teammate(const QString& teammateId) const;
    Teammate* findByName(const QString& name) const;
    QList<Teammate*> allTeammates() const;
    int teammateCount() const;

signals:
    void teammateCreated(const QString& teammateId);
    void teammateRemoved(const QString& teammateId);
    /// 队友完成回复，需要推送到会话
    void teammateReplied(const QString& teammateId, const QString& teammateName,
                         bool success, const QString& content);

private:
    explicit TeammateManager(QObject* parent = nullptr);
    Q_DISABLE_COPY(TeammateManager)

    QHash<QString, ITeammateBackend*> m_backends;   // backendId → backend
    QHash<QString, Teammate*> m_teammates;           // teammateId → teammate
};

#endif // TEAMMATEMANAGER_H
