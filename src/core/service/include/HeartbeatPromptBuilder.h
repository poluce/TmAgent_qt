#ifndef HEARTBEATPROMPTBUILDER_H
#define HEARTBEATPROMPTBUILDER_H

#include "HeartbeatTypes.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <functional>

class HeartbeatPromptBuilder {
public:
    struct Dependencies {
        std::function<QString(const QString&)> instructionPathForAgent;
        std::function<QString(const QString&, bool*)> readInstructionFile;
    };

    struct Context {
        QString agentId;
        HeartbeatTicket ticket;
        HeartbeatSnapshot snapshot;
        QStringList actionableSignals;
    };

    explicit HeartbeatPromptBuilder(const Dependencies& dependencies)
        : m_dependencies(dependencies)
    {
    }

    QString buildEscalationPrompt(const Context& context) const
    {
        const QString instruction = loadInstruction(context.agentId);

        QStringList sections;
        sections << QStringLiteral("【后台心跳升级任务】")
                 << QStringLiteral("ticket_kind=%1").arg(heartbeatTicketKindToString(context.ticket.kind))
                 << QStringLiteral("reason=%1").arg(context.ticket.reason.trimmed().isEmpty()
                                                        ? QStringLiteral("auto")
                                                        : context.ticket.reason.trimmed())
                 << QString();

        sections << QStringLiteral("[当前状态]")
                 << QStringLiteral("- provider_state: %1").arg(context.snapshot.providerState.isEmpty()
                                                                   ? QStringLiteral("unknown")
                                                                   : context.snapshot.providerState)
                 << QStringLiteral("- pulse_state: %1").arg(context.snapshot.pulseState.isEmpty()
                                                                ? QStringLiteral("unknown")
                                                                : context.snapshot.pulseState)
                 << QStringLiteral("- active_delegate_jobs: %1").arg(context.snapshot.activeDelegateJobCount)
                 << QStringLiteral("- scheduler_issue: %1").arg(context.snapshot.schedulerIssue
                                                                    ? QStringLiteral("true")
                                                                    : QStringLiteral("false"))
                 << QStringLiteral("- memory_issue: %1").arg(context.snapshot.memoryIssue
                                                                 ? QStringLiteral("true")
                                                                 : QStringLiteral("false"))
                 << QString();

        sections << QStringLiteral("[关键变化]")
                 << (context.actionableSignals.isEmpty()
                         ? QStringLiteral("- 当前无关键变化")
                         : QStringLiteral("- %1").arg(context.actionableSignals.join(QStringLiteral("\n- "))))
                 << QString();

        if (!instruction.trimmed().isEmpty()) {
            sections << QStringLiteral("[补充指令]") << instruction.trimmed() << QString();
        }

        sections << QStringLiteral("[输出要求]")
                 << QStringLiteral("1. 仅输出一段简短中文摘要，面向最终用户。")
                 << QStringLiteral("2. 只描述真正需要同步给用户的关键变化或风险。")
                 << QStringLiteral("3. 不要暴露内部链路、trace id、raw prompt。")
                 << QStringLiteral("4. 若为手动巡检且无关键变化，可明确回复“当前无关键变化”。");

        return sections.join(QStringLiteral("\n"));
    }

    static QString defaultTemplate()
    {
        return QStringLiteral(
            "## HEARTBEAT\n"
            "你是后台巡检升级阶段的补充指令。\n"
            "1. 优先总结真正值得用户知道的变化。\n"
            "2. 若只是内部噪声，不要放大表述。\n"
            "3. 输出保持简短、明确、克制。\n");
    }

    static void repairInstructionFileIfNeeded(const QString& path)
    {
        QFile file(path);
        if (!file.exists())
            return;
        if (!file.open(QFile::ReadOnly | QFile::Text))
            return;
        const QString decoded = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
        if (decoded == defaultTemplate().trimmed())
            return;
        if (decoded.contains(QStringLiteral("轻量心跳巡检")))
            writeUtf8TextFile(path, defaultTemplate());
    }

private:
    QString loadInstruction(const QString& agentId) const
    {
        QString path;
        if (m_dependencies.instructionPathForAgent)
            path = m_dependencies.instructionPathForAgent(agentId).trimmed();
        if (path.isEmpty())
            return defaultTemplate();

        repairInstructionFileIfNeeded(path);
        bool ok = false;
        QString text;
        if (m_dependencies.readInstructionFile)
            text = m_dependencies.readInstructionFile(path, &ok);
        if (!ok || text.trimmed().isEmpty())
            return defaultTemplate();
        return text.trimmed();
    }

    static bool writeUtf8TextFile(const QString& path, const QString& content)
    {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return false;
        QFile file(path);
        if (!file.open(QFile::WriteOnly | QFile::Text))
            return false;
        const QByteArray bytes = content.toUtf8();
        const bool ok = (file.write(bytes) == bytes.size());
        file.close();
        return ok;
    }

    Dependencies m_dependencies;
};

#endif // HEARTBEATPROMPTBUILDER_H
