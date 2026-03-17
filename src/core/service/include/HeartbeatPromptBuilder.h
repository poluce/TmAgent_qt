#ifndef HEARTBEATPROMPTBUILDER_H
#define HEARTBEATPROMPTBUILDER_H

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <functional>

class HeartbeatPromptBuilder {
public:
    struct Dependencies {
        std::function<QString(const QString&)> heartbeatPathForAgent;
    };

    explicit HeartbeatPromptBuilder(const Dependencies& dependencies)
        : m_dependencies(dependencies)
    {
    }

    QString build(const QString& agentId, const QString& reason) const
    {
        QString instruction;
        if (m_dependencies.heartbeatPathForAgent) {
            const QString path = m_dependencies.heartbeatPathForAgent(agentId);
            if (!path.trimmed().isEmpty()) {
                repairInstructionFileIfNeeded(path);
                QFile file(path);
                if (file.exists() && file.open(QFile::ReadOnly | QFile::Text)) {
                    instruction = decodePossiblyMojibakeUtf8(file.readAll()).trimmed();
                    file.close();
                }
            }
        }

        if (instruction.isEmpty()) {
            instruction = QStringLiteral(
                "请执行一次轻量心跳巡检：\n"
                "1) 回顾最近任务进度；\n"
                "2) 若有后台委派任务，汇总当前状态；\n"
                "3) 若无重要变更，默认静默（不发聊天消息）；手动触发时可简短回复“当前无关键更新”。");
        }

        const QString reasonLabel = reason.trimmed().isEmpty()
            ? QStringLiteral("interval")
            : reason.trimmed();
        return QStringLiteral("【系统心跳任务】reason=%1\n%2").arg(reasonLabel, instruction);
    }

    static QString defaultTemplate()
    {
        return QStringLiteral(
            "## HEARTBEAT\n"
            "你正在执行后台心跳巡检，请遵循：\n"
            "1. 优先汇总当前任务进度与风险。\n"
            "2. 若有子代理任务，先给出 job 状态摘要。\n"
            "3. 若无关键变化，默认静默（不发聊天消息），仅在手动触发时可回复“当前无关键更新”。\n"
            "4. 输出尽量简短，避免重复。\n");
    }

    static void repairInstructionFileIfNeeded(const QString& path)
    {
        QFile file(path);
        if (!file.exists())
            return;
        if (!file.open(QFile::ReadOnly | QFile::Text))
            return;
        const QByteArray raw = file.readAll();
        file.close();

        const QString decoded = QString::fromUtf8(raw);
        QString normalized = decodePossiblyMojibakeUtf8(raw);
        normalized = normalizeLegacyHeartbeatLine(normalized);
        if (decoded == normalized)
            return;
        writeUtf8TextFile(path, normalized);
    }

private:
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

    static bool containsCjk(const QString& text)
    {
        for (const QChar c : text) {
            const ushort u = c.unicode();
            if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF))
                return true;
        }
        return false;
    }

    static int latinMojibakeCharCount(const QString& text)
    {
        int count = 0;
        for (const QChar c : text) {
            const ushort u = c.unicode();
            if ((u >= 0x00C0 && u <= 0x00FF) || (u >= 0x00A1 && u <= 0x00BF))
                ++count;
        }
        return count;
    }

    static QString decodePossiblyMojibakeUtf8(const QByteArray& bytes)
    {
        const QString utf8Text = QString::fromUtf8(bytes);
        if (utf8Text.isEmpty())
            return utf8Text;
        if (containsCjk(utf8Text))
            return utf8Text;
        if (latinMojibakeCharCount(utf8Text) < 8)
            return utf8Text;

        const QString repaired = QString::fromUtf8(utf8Text.toLatin1());
        if (containsCjk(repaired))
            return repaired;
        return utf8Text;
    }

    static QString normalizeLegacyHeartbeatLine(const QString& input)
    {
        QString out = input;
        const QString replacement = QStringLiteral(
            "3. 若无关键变化，默认静默（不发聊天消息），仅在手动触发时可回复“当前无关键更新”。");
        const QStringList legacyHints = {
            QStringLiteral("3. 若无关键变化，返回“当前无关键更新”。"),
            QStringLiteral("3. 若无关键变化，返回\"当前无关键更新\"。"),
            QStringLiteral("3. 若无关键变化，返回“当前无关键更新”"),
            QStringLiteral("3. 若无关键变化，返回\"当前无关键更新\"")
        };
        for (const QString& legacy : legacyHints) {
            if (out.contains(legacy))
                out.replace(legacy, replacement);
        }
        return out;
    }

    Dependencies m_dependencies;
};

#endif // HEARTBEATPROMPTBUILDER_H
