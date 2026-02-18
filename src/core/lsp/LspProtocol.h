#ifndef LSPPROTOCOL_H
#define LSPPROTOCOL_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QString>

/**
 * @brief LSP 协议数据结构定义
 *
 * 仿照 Qt 6 的 qtlanguageserver 模块设计
 * 参考 LSP 规范: https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/
 */
namespace Lsp {

//=============================================================================
// 基础类型
//=============================================================================

/**
 * @brief 文档中的位置（行、列）
 */
struct Position {
    int line = 0;      // 0-based 行号
    int character = 0; // 0-based 字符偏移

    QJsonObject toJson() const
    {
        return { { "line", line }, { "character", character } };
    }

    static Position fromJson(const QJsonObject& obj)
    {
        return { obj["line"].toInt(), obj["character"].toInt() };
    }
};

/**
 * @brief 文档中的范围（起始位置到结束位置）
 */
struct Range {
    Position start;
    Position end;

    QJsonObject toJson() const
    {
        return { { "start", start.toJson() }, { "end", end.toJson() } };
    }

    static Range fromJson(const QJsonObject& obj)
    {
        return {
            Position::fromJson(obj["start"].toObject()),
            Position::fromJson(obj["end"].toObject())
        };
    }
};

/**
 * @brief 位置信息（文件URI + 范围）
 */
struct Location {
    QString uri;
    Range range;

    QJsonObject toJson() const
    {
        return { { "uri", uri }, { "range", range.toJson() } };
    }

    static Location fromJson(const QJsonObject& obj)
    {
        return { obj["uri"].toString(), Range::fromJson(obj["range"].toObject()) };
    }
};

//=============================================================================
// 诊断信息
//=============================================================================

/**
 * @brief 诊断严重程度
 */
enum class DiagnosticSeverity {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4
};

/**
 * @brief 诊断信息（错误、警告等）
 */
struct Diagnostic {
    Range range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    QString code;
    QString source;
    QString message;

    static Diagnostic fromJson(const QJsonObject& obj)
    {
        Diagnostic d;
        d.range = Range::fromJson(obj["range"].toObject());
        d.severity = static_cast<DiagnosticSeverity>(obj["severity"].toInt(1));
        d.code = obj["code"].toString();
        d.source = obj["source"].toString();
        d.message = obj["message"].toString();
        return d;
    }

    QString severityString() const
    {
        switch (severity) {
        case DiagnosticSeverity::Error:
            return "ERROR";
        case DiagnosticSeverity::Warning:
            return "WARNING";
        case DiagnosticSeverity::Information:
            return "INFO";
        case DiagnosticSeverity::Hint:
            return "HINT";
        }
        return "UNKNOWN";
    }
};

//=============================================================================
// 符号信息
//=============================================================================

/**
 * @brief 符号类型
 */
enum class SymbolKind {
    File = 1,
    Module = 2,
    Namespace = 3,
    Package = 4,
    Class = 5,
    Method = 6,
    Property = 7,
    Field = 8,
    Constructor = 9,
    Enum = 10,
    Interface = 11,
    Function = 12,
    Variable = 13,
    Constant = 14,
    String = 15,
    Number = 16,
    Boolean = 17,
    Array = 18,
    Object = 19,
    Key = 20,
    Null = 21,
    EnumMember = 22,
    Struct = 23,
    Event = 24,
    Operator = 25,
    TypeParameter = 26
};

/**
 * @brief 符号信息
 */
struct SymbolInformation {
    QString name;
    SymbolKind kind;
    Location location;
    QString containerName;

    static SymbolInformation fromJson(const QJsonObject& obj)
    {
        SymbolInformation s;
        s.name = obj["name"].toString();
        s.kind = static_cast<SymbolKind>(obj["kind"].toInt());
        s.location = Location::fromJson(obj["location"].toObject());
        s.containerName = obj["containerName"].toString();
        return s;
    }
};

/**
 * @brief 文档符号（层级结构）
 */
struct DocumentSymbol {
    QString name;
    QString detail;
    SymbolKind kind;
    Range range;
    Range selectionRange;
    QList<DocumentSymbol> children;

    static DocumentSymbol fromJson(const QJsonObject& obj)
    {
        DocumentSymbol s;
        s.name = obj["name"].toString();
        s.detail = obj["detail"].toString();
        s.kind = static_cast<SymbolKind>(obj["kind"].toInt());
        s.range = Range::fromJson(obj["range"].toObject());
        s.selectionRange = Range::fromJson(obj["selectionRange"].toObject());

        QJsonArray childrenArr = obj["children"].toArray();
        for (const auto& child : childrenArr) {
            s.children.append(fromJson(child.toObject()));
        }
        return s;
    }
};

//=============================================================================
// 悬停信息
//=============================================================================

/**
 * @brief 悬停内容
 */
struct Hover {
    QString contents; // Markdown 格式
    Range range;

    static Hover fromJson(const QJsonObject& obj)
    {
        Hover h;

        // contents 可能是字符串、对象或数组
        QJsonValue contentsVal = obj["contents"];
        if (contentsVal.isString()) {
            h.contents = contentsVal.toString();
        } else if (contentsVal.isObject()) {
            QJsonObject contentsObj = contentsVal.toObject();
            h.contents = contentsObj["value"].toString();
        } else if (contentsVal.isArray()) {
            QStringList parts;
            for (const auto& item : contentsVal.toArray()) {
                if (item.isString()) {
                    parts << item.toString();
                } else if (item.isObject()) {
                    parts << item.toObject()["value"].toString();
                }
            }
            h.contents = parts.join("\n\n");
        }

        if (obj.contains("range")) {
            h.range = Range::fromJson(obj["range"].toObject());
        }
        return h;
    }
};

//=============================================================================
// 调用层级 (Call Hierarchy)
//=============================================================================

/**
 * @brief 调用层级项
 */
struct CallHierarchyItem {
    QString name;
    SymbolKind kind;
    QString detail;
    QString uri;
    Range range;
    Range selectionRange;
    QJsonObject data; // 存一些 opaque 数据，供后续请求使用

    static CallHierarchyItem fromJson(const QJsonObject& obj)
    {
        CallHierarchyItem item;
        item.name = obj["name"].toString();
        item.kind = static_cast<SymbolKind>(obj["kind"].toInt());
        item.detail = obj["detail"].toString();
        item.uri = obj["uri"].toString();
        item.range = Range::fromJson(obj["range"].toObject());
        item.selectionRange = Range::fromJson(obj["selectionRange"].toObject());
        item.data = obj["data"].toObject();
        return item;
    }

    QJsonObject toJson() const
    {
        return {
            { "name", name },
            { "kind", static_cast<int>(kind) },
            { "detail", detail },
            { "uri", uri },
            { "range", range.toJson() },
            { "selectionRange", selectionRange.toJson() },
            { "data", data }
        };
    }
};

/**
 * @brief 呼入调用
 */
struct CallHierarchyIncomingCall {
    CallHierarchyItem from;
    QList<Range> fromRanges;

    static CallHierarchyIncomingCall fromJson(const QJsonObject& obj)
    {
        CallHierarchyIncomingCall call;
        call.from = CallHierarchyItem::fromJson(obj["from"].toObject());
        QJsonArray rangesArr = obj["fromRanges"].toArray();
        for (const auto& r : rangesArr) {
            call.fromRanges.append(Range::fromJson(r.toObject()));
        }
        return call;
    }
};

/**
 * @brief 呼出调用
 */
struct CallHierarchyOutgoingCall {
    CallHierarchyItem to;
    QList<Range> fromRanges;

    static CallHierarchyOutgoingCall fromJson(const QJsonObject& obj)
    {
        CallHierarchyOutgoingCall call;
        call.to = CallHierarchyItem::fromJson(obj["to"].toObject());
        QJsonArray rangesArr = obj["fromRanges"].toArray();
        for (const auto& r : rangesArr) {
            call.fromRanges.append(Range::fromJson(r.toObject()));
        }
        return call;
    }
};

//=============================================================================
// 辅助函数
//=============================================================================

/**
 * @brief 规范化路径（处理 Windows 下的 /c/ 或 /mnt/c/ 形式）
 */
inline QString normalizePath(const QString& filePath)
{
    QString normalized = filePath;
    normalized.replace('\\', '/');
#ifdef Q_OS_WIN
    QRegularExpression re("^/(?:mnt/)?([a-zA-Z])/(.*)$");
    QRegularExpressionMatch match = re.match(normalized);
    if (match.hasMatch()) {
        const QString drive = match.captured(1).toUpper();
        const QString rest = match.captured(2);
        normalized = drive + ":/" + rest;
    }
#endif
    return normalized;
}

/**
 * @brief 将文件路径转为 LSP URI 格式
 */
inline QString pathToUri(const QString& filePath)
{
    QString normalized = normalizePath(filePath);
#ifdef Q_OS_WIN
    // If already a Windows drive path, use file:///C:/...
    if (QRegularExpression("^[A-Za-z]:/").match(normalized).hasMatch()) {
        return "file:///" + normalized;
    }
#endif
    if (!normalized.startsWith('/')) {
        normalized = '/' + normalized;
    }
    return "file://" + normalized;
}

/**
 * @brief 将 LSP URI 转为文件路径
 */
inline QString uriToPath(const QString& uri)
{
    QString path = uri;
    if (path.startsWith("file:///")) {
        path = path.mid(8); // 移除 "file:///"
    } else if (path.startsWith("file://")) {
        path = path.mid(7); // 移除 "file://"
    }
    // Windows 路径处理
#ifdef Q_OS_WIN
    if (path.startsWith('/') && path.length() > 2 && path[2] == ':') {
        path = path.mid(1); // 移除开头的 /
    }
#endif
    return path;
}

} // namespace Lsp

#endif // LSPPROTOCOL_H
