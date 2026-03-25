#include "HeartbeatExecutionService.h"

#include "TurnManager.h"

namespace {

TurnTask makeSyntheticTurn(const QString& agentId, const QString& reason)
{
    TurnTask turn;
    turn.actorIdentityId = agentId.trimmed();
    turn.turnId = QStringLiteral("heartbeat-%1").arg(reason.trimmed().isEmpty() ? QStringLiteral("maintenance")
                                                                                : reason.trimmed());
    turn.requestTraceId = turn.turnId + QStringLiteral("-trace");
    turn.runId = turn.turnId + QStringLiteral("-run");
    return turn;
}

} // namespace

QString HeartbeatExecutionService::ensureSessionForMaintenance(const QString& agentId) const
{
    return m_dependencies.resolvePrimarySessionForAgent
        ? m_dependencies.resolvePrimarySessionForAgent(
              agentId, true, false, QStringLiteral("heartbeat"))
        : QString();
}

void HeartbeatExecutionService::runMaintenance(const QString& sessionId,
                                               const QString& agentId,
                                               const HeartbeatPolicy& policy,
                                               const HeartbeatTicket& ticket) const
{
    if (sessionId.trimmed().isEmpty() || agentId.trimmed().isEmpty())
        return;

    const TurnTask syntheticTurn = makeSyntheticTurn(agentId, ticket.reason);
    if (policy.maintenancePolicy.reflectMemory && m_dependencies.reflectMemory) {
        m_dependencies.reflectMemory(
            sessionId, agentId, syntheticTurn, true, QStringLiteral("heartbeat_maintenance"));
    }
    Q_UNUSED(m_dependencies.rebuildMemoryIndex);
}

QString HeartbeatExecutionService::buildEscalationPrompt(
    const QString& agentId,
    const HeartbeatTicket& ticket,
    const HeartbeatSnapshot& snapshot,
    const QStringList& actionableSignals) const
{
    return m_dependencies.buildEscalationPrompt
        ? m_dependencies.buildEscalationPrompt(agentId, ticket, snapshot, actionableSignals)
        : QString();
}

QString HeartbeatExecutionService::buildFallbackSummary(const HeartbeatTicket& ticket,
                                                        const HeartbeatSnapshot& snapshot,
                                                        const QStringList& actionableSignals) const
{
    QStringList lines;
    lines << QStringLiteral("后台巡检发现以下关键变化：");
    if (actionableSignals.isEmpty()) {
        lines << QStringLiteral("- 当前无关键变化。");
    } else {
        for (const QString& signal : actionableSignals) {
            if (signal == QLatin1String("delegate_jobs")) {
                lines << QStringLiteral("- 委派任务状态发生变化，当前活跃任务数：%1。")
                             .arg(snapshot.activeDelegateJobCount);
            } else if (signal == QLatin1String("provider_status")) {
                lines << QStringLiteral("- Provider 状态变为：%1。").arg(snapshot.providerState);
            } else if (signal == QLatin1String("pulse_state")) {
                lines << QStringLiteral("- 主代理运行状态变为：%1。").arg(snapshot.pulseState);
            } else if (signal == QLatin1String("scheduler_issue")) {
                lines << QStringLiteral("- 定时任务出现需要关注的问题。");
            } else if (signal == QLatin1String("memory_issue")) {
                lines << QStringLiteral("- 记忆维护出现需要关注的问题。");
            } else if (signal == QLatin1String("restart_recovery")) {
                lines << QStringLiteral("- 软件重启后已恢复未完成的后台巡检。");
            }
        }
    }
    if (ticket.kind == HeartbeatTicketKind::Manual)
        lines << QStringLiteral("这是一次手动巡检结果。");
    return lines.join(QStringLiteral("\n"));
}
