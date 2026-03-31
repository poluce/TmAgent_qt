#ifndef TOOLFAILURESUPPORT_H
#define TOOLFAILURESUPPORT_H

#include "ToolTypes.h"
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace ToolFailureSupport {

inline bool containsAny(const QString& haystack, const QStringList& needles)
{
    for (const QString& needle : needles) {
        if (!needle.isEmpty() && haystack.contains(needle))
            return true;
    }
    return false;
}

inline void setStringIfMissing(QJsonObject& obj, const QString& key, const QString& value)
{
    if (!obj.value(key).toString().trimmed().isEmpty() || value.trimmed().isEmpty())
        return;
    obj.insert(key, value.trimmed());
}

inline void setBoolIfMissing(QJsonObject& obj, const QString& key, bool value)
{
    if (obj.value(key).isBool())
        return;
    obj.insert(key, value);
}

inline QJsonObject inferFailureMetadata(const QString& toolName,
                                        const QJsonObject& input,
                                        const QString& rawContent,
                                        const QJsonObject& existingData = QJsonObject())
{
    QJsonObject data = existingData;
    const QString raw = rawContent.trimmed();
    const QString lower = raw.toLower();
    const QString normalizedTool = toolName.trimmed().toLower();

    if (!data.value(QStringLiteral("error_category")).toString().trimmed().isEmpty()) {
        setBoolIfMissing(data, QStringLiteral("retryable"), true);
        setBoolIfMissing(data, QStringLiteral("ask_user_required"), false);
        if (data.value(QStringLiteral("error_category")).toString() == QLatin1String("validation_error")
            && data.value(QStringLiteral("next_action_hint")).toString().trimmed().isEmpty()) {
            const QString field = data.value(QStringLiteral("failing_field")).toString().trimmed();
            setStringIfMissing(
                data,
                QStringLiteral("next_action_hint"),
                field.isEmpty()
                    ? QStringLiteral("根据期望格式修正参数后重试，不要原样重复失败输入")
                    : QStringLiteral("只修正 %1 后重试，其余参数保持不变").arg(field));
        }
        return data;
    }

    auto setValidationDefaults = [&data]() {
        setStringIfMissing(data, QStringLiteral("error_category"), QStringLiteral("validation_error"));
        setBoolIfMissing(data, QStringLiteral("retryable"), true);
        setBoolIfMissing(data, QStringLiteral("ask_user_required"), false);
        const QString field = data.value(QStringLiteral("failing_field")).toString().trimmed();
        setStringIfMissing(
            data,
            QStringLiteral("next_action_hint"),
            field.isEmpty()
                ? QStringLiteral("根据期望格式修正参数后重试，不要原样重复失败输入")
                : QStringLiteral("只修正 %1 后重试，其余参数保持不变").arg(field));
    };

    if (data.contains(QStringLiteral("failing_field"))
        || containsAny(lower,
                       QStringList()
                           << QStringLiteral("不能为空")
                           << QStringLiteral("必须提供")
                           << QStringLiteral("must provide")
                           << QStringLiteral("required")
                           << QStringLiteral("invalid argument")
                           << QStringLiteral("validation")
                           << QStringLiteral("参数校验")
                           << QStringLiteral("缺少 url")
                           << QStringLiteral("run_at 必须晚于当前时间"))) {
        if (normalizedTool == QLatin1String("web_fetch") && lower.contains(QStringLiteral("缺少 url"))) {
            setStringIfMissing(data, QStringLiteral("failing_field"), QStringLiteral("url"));
            setStringIfMissing(data, QStringLiteral("expected_format"), QStringLiteral("提供完整 url；若只有关键词，请改用 websearch(query)"));
            setStringIfMissing(data, QStringLiteral("next_action_hint"), QStringLiteral("改用 websearch(query) 或补全 url 后再试"));
        }
        setValidationDefaults();
        return data;
    }

    if (normalizedTool == QLatin1String("execute_command")) {
        if (containsAny(lower,
                        QStringList()
                            << QStringLiteral("安全策略拒绝")
                            << QStringLiteral("校验失败")
                            << QStringLiteral("blacklist")
                            << QStringLiteral("policy rejected"))) {
            setStringIfMissing(data, QStringLiteral("error_category"), QStringLiteral("policy_blocked"));
            setBoolIfMissing(data, QStringLiteral("retryable"), true);
            setBoolIfMissing(data, QStringLiteral("ask_user_required"), false);
            setStringIfMissing(
                data,
                QStringLiteral("next_action_hint"),
                QStringLiteral("改用受允许的替代命令或更专用的工具，不要原样重复同一命令"));
            return data;
        }

        if (containsAny(lower,
                        QStringList()
                            << QStringLiteral("not found")
                            << QStringLiteral("is not recognized as an internal or external command")
                            << QStringLiteral("command not found")
                            << QStringLiteral("不是内部或外部命令")
                            << QStringLiteral("找不到指定的文件"))) {
            setStringIfMissing(data, QStringLiteral("error_category"), QStringLiteral("executable_missing"));
            setBoolIfMissing(data, QStringLiteral("retryable"), true);
            setBoolIfMissing(data, QStringLiteral("ask_user_required"), false);
            setStringIfMissing(
                data,
                QStringLiteral("next_action_hint"),
                QStringLiteral("改用已存在的命令，或先确认依赖是否已安装并在 PATH 中可见"));
            return data;
        }

        if (containsAny(lower,
                        QStringList()
                            << QStringLiteral("permission denied")
                            << QStringLiteral("access is denied")
                            << QStringLiteral("工作目录越界")
                            << QStringLiteral("必须位于当前助手工作空间内")
                            << QStringLiteral("无法操作 ")
                            << QStringLiteral("写入操作只能在工作目录")
                            << QStringLiteral("denied"))) {
            setStringIfMissing(data, QStringLiteral("error_category"), QStringLiteral("path_or_permission_error"));
            setBoolIfMissing(data, QStringLiteral("retryable"), true);
            setBoolIfMissing(data, QStringLiteral("ask_user_required"), false);
            setStringIfMissing(
                data,
                QStringLiteral("next_action_hint"),
                QStringLiteral("先修正 working_directory、目标路径或权限前提，再重试"));
            return data;
        }

        if (containsAny(lower,
                        QStringList()
                            << QStringLiteral("no such file or directory")
                            << QStringLiteral("cannot find the path specified")
                            << QStringLiteral("系统找不到指定的路径")
                            << QStringLiteral("环境变量")
                            << QStringLiteral("workspace"))) {
            setStringIfMissing(data, QStringLiteral("error_category"), QStringLiteral("environment_error"));
            setBoolIfMissing(data, QStringLiteral("retryable"), true);
            setBoolIfMissing(data, QStringLiteral("ask_user_required"), false);
            setStringIfMissing(
                data,
                QStringLiteral("next_action_hint"),
                QStringLiteral("先修正环境变量、工作目录或依赖前提，再重试"));
            return data;
        }

        if (containsAny(lower, QStringList() << QStringLiteral("超时") << QStringLiteral("timed out"))) {
            setStringIfMissing(data, QStringLiteral("error_category"), QStringLiteral("execution_timeout"));
            setBoolIfMissing(data, QStringLiteral("retryable"), true);
            setBoolIfMissing(data, QStringLiteral("ask_user_required"), false);
            setStringIfMissing(
                data,
                QStringLiteral("next_action_hint"),
                QStringLiteral("缩小命令范围、降低输出规模，或改用更专用的工具"));
            return data;
        }

        if (containsAny(lower, QStringList() << QStringLiteral("用户拒绝执行") << QStringLiteral("user rejected"))) {
            setStringIfMissing(data, QStringLiteral("error_category"), QStringLiteral("user_confirmation_required"));
            setBoolIfMissing(data, QStringLiteral("retryable"), false);
            setBoolIfMissing(data, QStringLiteral("ask_user_required"), true);
            setStringIfMissing(
                data,
                QStringLiteral("next_action_hint"),
                QStringLiteral("如确需执行，请先向用户说明风险并请求确认"));
            return data;
        }
    }

    if (containsAny(lower,
                    QStringList()
                        << QStringLiteral("权限")
                        << QStringLiteral("permission denied")
                        << QStringLiteral("access is denied")
                        << QStringLiteral("文件不存在")
                        << QStringLiteral("目录不存在")
                        << QStringLiteral("无法读取文件")
                        << QStringLiteral("无法写入文件")
                        << QStringLiteral("cannot open")
                        << QStringLiteral("cannot write"))) {
        setStringIfMissing(data, QStringLiteral("error_category"), QStringLiteral("path_or_permission_error"));
        setBoolIfMissing(data, QStringLiteral("retryable"), true);
        setBoolIfMissing(data, QStringLiteral("ask_user_required"), false);
        setStringIfMissing(
            data,
            QStringLiteral("next_action_hint"),
            QStringLiteral("先修正目标路径、文件存在性或权限前提，再重试"));
        return data;
    }

    if (containsAny(lower,
                    QStringList()
                        << QStringLiteral("环境")
                        << QStringLiteral("not found")
                        << QStringLiteral("no such file or directory")
                        << QStringLiteral("cannot find")
                        << QStringLiteral("找不到"))) {
        setStringIfMissing(data, QStringLiteral("error_category"), QStringLiteral("environment_error"));
        setBoolIfMissing(data, QStringLiteral("retryable"), true);
        setBoolIfMissing(data, QStringLiteral("ask_user_required"), false);
        setStringIfMissing(
            data,
            QStringLiteral("next_action_hint"),
            QStringLiteral("先修正环境前提或依赖，再重试"));
        return data;
    }

    Q_UNUSED(input);
    return data;
}

inline QString appendMissingStructuredFields(const QString& rawContent, const QJsonObject& data)
{
    if (data.isEmpty())
        return rawContent;

    const QString raw = rawContent.trimmed();
    struct OrderedKey {
        const char* name;
    };
    const OrderedKey orderedKeys[] = {
        { "error_category" },
        { "retryable" },
        { "ask_user_required" },
        { "next_action_hint" },
        { "failing_field" },
        { "bad_value" },
        { "expected_format" },
        { "suggested_value" },
        { "retry_rule" },
        { "do_not_repeat" }
    };

    QStringList lines;
    for (const OrderedKey& item : orderedKeys) {
        const QString key = QString::fromLatin1(item.name);
        if (!data.contains(key) || raw.contains(key + QStringLiteral(":")))
            continue;
        const QJsonValue value = data.value(key);
        if (value.isBool())
            lines.append(QStringLiteral("%1: %2").arg(key, value.toBool() ? QStringLiteral("true")
                                                                          : QStringLiteral("false")));
        else {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty())
                lines.append(QStringLiteral("%1: %2").arg(key, text));
        }
    }

    if (lines.isEmpty())
        return rawContent;

    if (raw.isEmpty())
        return lines.join(QStringLiteral("\n"));
    return raw + QStringLiteral("\n") + lines.join(QStringLiteral("\n"));
}

inline ToolResult enrichFailureResult(const QString& toolName,
                                      const QJsonObject& input,
                                      const ToolResult& original)
{
    if (original.success)
        return original;

    ToolResult enriched = original;
    enriched.data = inferFailureMetadata(toolName, input, original.rawContent, original.data);
    enriched.rawContent = appendMissingStructuredFields(original.rawContent, enriched.data);
    return enriched;
}

} // namespace ToolFailureSupport

#endif // TOOLFAILURESUPPORT_H
