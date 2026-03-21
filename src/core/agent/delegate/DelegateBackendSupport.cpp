#include "DelegateBackendSupport.h"

#include <QRegularExpression>

namespace DelegateBackendInternal {
namespace {

static constexpr int kStructuredRawOutputMaxChars = 1200;

QString truncateForData(const QString& text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars) + QStringLiteral("\n...[truncated]...");
}

QStringList collectReportLines(const QString& text, int maxLines)
{
    QStringList out;
    const QStringList sourceLines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (QString line : sourceLines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QStringLiteral("STATUS:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("DONE:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("PENDING:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("EVIDENCE:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("RISKS:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("NEXT:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("RAW_OUTPUT:"), Qt::CaseInsensitive)) {
            continue;
        }
        if (line.startsWith(QLatin1Char('-')))
            line = line.mid(1).trimmed();
        if (line.size() > 200)
            line = line.left(200) + QStringLiteral("...");
        out.append(line);
        if (out.size() >= maxLines)
            break;
    }
    return out;
}

} // namespace

QString buildExecutionPrompt(const QString& task)
{
    QString prompt = QStringLiteral("请执行以下任务，并在必要时使用工具。\n任务：\n%1\n").arg(task);
    prompt += QStringLiteral(
        "\n执行要求：\n"
        "1) 先验证路径，再扩展；避免无证据重复调用同一工具。\n"
        "2) 遇到错误先修正前提，不要盲目重试。\n"
        "3) 结束时必须输出结构化报告（严格按以下标签，不要省略）：\n"
        "STATUS: COMPLETED 或 PARTIAL 或 BLOCKED\n"
        "DONE:\n"
        "- 已完成项\n"
        "PENDING:\n"
        "- 未完成项\n"
        "EVIDENCE:\n"
        "- 关键证据（命令/输出/路径）\n"
        "RISKS:\n"
        "- 风险/阻塞\n"
        "NEXT:\n"
        "- 下一步建议\n");
    return prompt;
}

QString normalizeDelegateBackend(const QString& rawBackend)
{
    const QString lowered = rawBackend.trimmed().toLower();
    if (lowered == QLatin1String("codex"))
        return QStringLiteral("codex");
    return QStringLiteral("tmagent");
}

QString extractStatusTag(const QString& text)
{
    static const QRegularExpression re(
        QStringLiteral("(?im)^\\s*STATUS\\s*:\\s*(COMPLETED|PARTIAL|BLOCKED)\\b"));
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return QString();
    return match.captured(1).trimmed().toUpper();
}

bool isBlockedStatus(const QString& statusTag)
{
    return statusTag == QLatin1String("BLOCKED");
}

QString canonicalStatusTag(const QString& rawStatus)
{
    const QString status = rawStatus.trimmed().toUpper();
    if (status == QLatin1String("COMPLETED")
        || status == QLatin1String("PARTIAL")
        || status == QLatin1String("BLOCKED")) {
        return status;
    }
    return QStringLiteral("PARTIAL");
}

QString ensureStructuredDelegateOutput(
    const QString& task,
    const QString& rawText,
    const QString& statusHint,
    bool* normalizedByScheduler)
{
    if (normalizedByScheduler)
        *normalizedByScheduler = false;

    const QString text = rawText.trimmed();
    const QString status = canonicalStatusTag(statusHint.isEmpty() ? extractStatusTag(text) : statusHint);
    const bool hasStatus = QRegularExpression(
                               QStringLiteral("(?im)^\\s*STATUS\\s*:\\s*(COMPLETED|PARTIAL|BLOCKED)\\b"))
                               .match(text)
                               .hasMatch();
    const bool hasEvidence = QRegularExpression(QStringLiteral("(?im)^\\s*EVIDENCE\\s*:")).match(text).hasMatch();
    const bool hasNext = QRegularExpression(QStringLiteral("(?im)^\\s*NEXT\\s*:")).match(text).hasMatch();
    if (hasStatus && hasEvidence && hasNext)
        return text;

    if (normalizedByScheduler)
        *normalizedByScheduler = true;

    const QStringList lines = collectReportLines(text, 6);
    QStringList report;
    report << QStringLiteral("[Sub-agent Report]");
    report << QStringLiteral("STATUS: %1").arg(status);
    if (!task.trimmed().isEmpty())
        report << QStringLiteral("TASK: %1").arg(task.trimmed().left(240));
    report << QStringLiteral("DONE:");
    if (!lines.isEmpty()) {
        for (const QString& line : lines)
            report << QStringLiteral("- %1").arg(line);
    } else {
        report << QStringLiteral("- (未提取到明确完成项)");
    }
    report << QStringLiteral("PENDING:");
    if (status == QLatin1String("COMPLETED"))
        report << QStringLiteral("- (无)");
    else
        report << QStringLiteral("- 需要主代理补充约束或继续拆分任务。");
    report << QStringLiteral("EVIDENCE:");
    if (!lines.isEmpty()) {
        for (const QString& line : lines.mid(0, 3))
            report << QStringLiteral("- %1").arg(line);
    } else if (!text.isEmpty()) {
        report << QStringLiteral("- 已返回文本输出，详见 RAW_OUTPUT。");
    } else {
        report << QStringLiteral("- 子代理未返回有效正文。");
    }
    report << QStringLiteral("RISKS:");
    if (status == QLatin1String("BLOCKED"))
        report << QStringLiteral("- 当前存在阻塞，需要补充信息/权限/路径。");
    else if (status == QLatin1String("PARTIAL"))
        report << QStringLiteral("- 当前结果可能不完整，建议复核后继续。");
    else
        report << QStringLiteral("- (无明显风险)");
    report << QStringLiteral("NEXT:");
    if (status == QLatin1String("COMPLETED"))
        report << QStringLiteral("- 主代理可汇总结果并向用户确认是否继续深化。");
    else
        report << QStringLiteral("- 主代理应先补齐阻塞条件，再发起下一轮委派。");
    if (!text.isEmpty()) {
        report << QStringLiteral("RAW_OUTPUT:");
        report << truncateForData(text, kStructuredRawOutputMaxChars);
    }
    return report.join(QStringLiteral("\n"));
}

} // namespace DelegateBackendInternal
