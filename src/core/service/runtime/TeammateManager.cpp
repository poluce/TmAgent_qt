#include "TeammateManager.h"

#include "core/persistence/ChatPersistenceService.h"
#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QUuid>

namespace {

QString normalizePersistenceValue(const QString& raw)
{
    return raw.trimmed().compare(QStringLiteral("temporary"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("temporary")
        : QStringLiteral("persistent");
}

QString defaultWorkspaceForConfig(const Teammate::Config& config, const QString& teammateId)
{
    ChatPersistenceService persistence;
    return QDir(QDir(persistence.agentsDirPath()).filePath(config.ownerAgentId.trimmed()))
        .filePath(QStringLiteral("teammates/%1/workspace").arg(teammateId));
}

} // namespace

TeammateManager* TeammateManager::instance()
{
    static TeammateManager mgr(nullptr);
    return &mgr;
}

TeammateManager::TeammateManager(QObject* parent)
    : QObject(parent)
{
}

// ── 后端注册 ──

void TeammateManager::registerBackend(ITeammateBackend* backend)
{
    if (!backend || backend->backendId().isEmpty())
        return;
    m_backends.insert(backend->backendId(), backend);
}

ITeammateBackend* TeammateManager::backend(const QString& backendId) const
{
    return m_backends.value(backendId);
}

QStringList TeammateManager::registeredBackendIds() const
{
    return m_backends.keys();
}

// ── 队友生命周期 ──

TeammateManager::CreateResult TeammateManager::createTeammate(const Teammate::Config& config)
{
    CreateResult out;

    if (config.name.trimmed().isEmpty()) {
        out.error = QStringLiteral("队友名称不能为空");
        return out;
    }

    if (findByNameForOwner(config.name.trimmed(), config.ownerAgentId)) {
        out.error = QStringLiteral("已存在同名队友: %1").arg(config.name);
        return out;
    }

    const QString backendId = config.backend.trimmed().isEmpty()
        ? QStringLiteral("codex") : config.backend.trimmed();

    ITeammateBackend* be = m_backends.value(backendId);
    if (!be) {
        out.error = QStringLiteral("未注册的后端: %1，可用后端: %2")
            .arg(backendId, registeredBackendIds().join(QStringLiteral(", ")));
        return out;
    }

    // 确保后端就绪
    QString readyError;
    if (!be->ensureReady(&readyError)) {
        out.error = QStringLiteral("%1 后端启动失败: %2").arg(backendId, readyError);
        return out;
    }

    const QString teammateId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Teammate::Config normalized = config;
    normalized.persistence = normalizePersistenceValue(config.persistence);
    normalized.autoCleanup =
        config.autoCleanup || normalized.persistence == QLatin1String("temporary");
    if (normalized.workingDirectory.trimmed().isEmpty())
        normalized.workingDirectory = defaultWorkspaceForConfig(normalized, teammateId);
    QDir().mkpath(normalized.workingDirectory);

    auto* mate = new Teammate(teammateId, normalized, this);
    m_teammates.insert(teammateId, mate);

    // 通过后端创建会话
    const ITeammateBackend::CreateResult sessionResult = be->createSession(mate);
    if (!sessionResult.success) {
        m_teammates.remove(teammateId);
        mate->deleteLater();
        out.error = sessionResult.error;
        return out;
    }

    out.success = true;
    out.teammateId = teammateId;
    out.threadId = sessionResult.threadId;

    // 监听队友回复，异步推送
    connect(mate, &Teammate::turnCompleted, this,
        [this, teammateId](const QString&, bool success, const QString& content) {
            Teammate* m = m_teammates.value(teammateId);
            if (!m)
                return;
            emit teammateReplied(teammateId, m->name(), success, content, m->threadId());
            if (m->autoCleanup()) {
                QMetaObject::invokeMethod(this, [this, teammateId]() {
                    removeTeammate(teammateId, nullptr);
                }, Qt::QueuedConnection);
            }
        });

    emit teammateCreated(teammateId);
    return out;
}

bool TeammateManager::removeTeammate(const QString& teammateId, QString* error)
{
    Teammate* mate = m_teammates.value(teammateId);
    if (!mate) {
        if (error)
            *error = QStringLiteral("队友不存在: %1").arg(teammateId);
        return false;
    }

    // 通知后端销毁会话
    ITeammateBackend* be = m_backends.value(mate->backend());
    if (be)
        be->destroySession(mate);

    m_teammates.remove(teammateId);
    mate->setStatus(Teammate::Status::Shutdown);
    mate->deleteLater();

    emit teammateRemoved(teammateId);
    return true;
}

// ── 对话 ──

TeammateManager::MessageResult TeammateManager::sendMessage(const QString& teammateId, const QString& text)
{
    MessageResult out;

    Teammate* mate = m_teammates.value(teammateId);
    if (!mate) {
        out.error = QStringLiteral("队友不存在: %1").arg(teammateId);
        return out;
    }

    if (mate->status() == Teammate::Status::Busy) {
        out.error = QStringLiteral("队友 \"%1\" 正忙，请等待当前 Turn 完成").arg(mate->name());
        return out;
    }

    if (mate->status() == Teammate::Status::Shutdown) {
        out.error = QStringLiteral("队友 \"%1\" 已关闭").arg(mate->name());
        return out;
    }

    // Error 状态的队友允许重新发消息（自动恢复）
    if (mate->status() == Teammate::Status::Error) {
        qInfo() << "[TeammateManager] 队友" << mate->name()
                << "从 Error 状态自动恢复，上次错误:" << mate->lastError();
    }

    ITeammateBackend* be = m_backends.value(mate->backend());
    if (!be) {
        out.error = QStringLiteral("后端 \"%1\" 未注册").arg(mate->backend());
        return out;
    }

    if (!be->isReady()) {
        out.error = QStringLiteral("后端 \"%1\" 未就绪").arg(mate->backend());
        return out;
    }

    mate->setStatus(Teammate::Status::Busy);
    mate->setLastError(QString());
    mate->touchLastActive();

    const ITeammateBackend::SendResult sendResult = be->sendMessage(mate, text);
    if (!sendResult.success) {
        mate->setStatus(Teammate::Status::Error);
        mate->setLastError(sendResult.error);
        out.error = sendResult.error;
        return out;
    }

    out.success = true;
    out.turnId = sendResult.turnId;
    return out;
}

bool TeammateManager::cancelTeammateTurn(const QString& teammateId, QString* error)
{
    Teammate* mate = m_teammates.value(teammateId);
    if (!mate) {
        if (error)
            *error = QStringLiteral("队友不存在: %1").arg(teammateId);
        return false;
    }

    ITeammateBackend* be = m_backends.value(mate->backend());
    if (!be) {
        if (error)
            *error = QStringLiteral("后端 \"%1\" 未注册").arg(mate->backend());
        return false;
    }

    return be->cancelTurn(mate, error);
}

// ── 查询 ──

Teammate* TeammateManager::teammate(const QString& teammateId) const
{
    return m_teammates.value(teammateId);
}

Teammate* TeammateManager::findByName(const QString& name) const
{
    const QString trimmed = name.trimmed();
    for (auto* mate : m_teammates) {
        if (mate->name().trimmed().compare(trimmed, Qt::CaseInsensitive) == 0)
            return mate;
    }
    return nullptr;
}

Teammate* TeammateManager::findByNameForOwner(const QString& name, const QString& ownerAgentId) const
{
    const QString trimmed = name.trimmed();
    for (auto* mate : m_teammates) {
        if (mate->ownerAgentId() == ownerAgentId
            && mate->name().trimmed().compare(trimmed, Qt::CaseInsensitive) == 0)
            return mate;
    }
    return nullptr;
}

QList<Teammate*> TeammateManager::allTeammates() const
{
    return m_teammates.values();
}

QList<Teammate*> TeammateManager::teammatesForOwner(const QString& ownerAgentId) const
{
    QList<Teammate*> result;
    for (auto* mate : m_teammates) {
        if (mate->ownerAgentId() == ownerAgentId)
            result.append(mate);
    }
    return result;
}

int TeammateManager::teammateCount() const
{
    return m_teammates.size();
}
