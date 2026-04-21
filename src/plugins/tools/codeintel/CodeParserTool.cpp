#include "CodeParserTool.h"
#include <tmagent/support/ToolSchemaSupport.h>

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QStringList>
#include <QTextStream>

QList<TmAgent::Tool> CodeParserTool::toolSchemas()
{
    return {
        makeToolSchema(
            E_OUTLINE),
            QStringLiteral("解析代码文件，提取所有函数、类、结构体的大纲信息。"),
            QJsonObject {
                { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要解析的代码文件绝对路径（目前仅支持 C++）")) }
            },
            QStringList { QStringLiteral("file_path") }),
        makeToolSchema(
            QString::fromLatin1(VIEW_CODE_ITEM),
            QStringLiteral("查看指定函数或类的完整代码。"),
            QJsonObject {
                { QStringLiteral("file_path")ingLiteral("string"), QStringLiteral("代码文件绝对路径")) },
                { QStringLiteral("item_name"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要查看的代码项名称")) }
            },
            QStringList { QStringLiteral("file_path"), QStringLiteral("item_name") })
    };
}

// ==================== 工具执行入口 ====================

QString CodeParserTool::executeViewFileOutline(const QJsonObject& input, TmAgent::IToolPluginHost* host)
{
    QString filePath = input["file_path"].toString();
 << "[CodeParserTool] 查看文件大纲:" << filePath;
    return viewFileOutline(filePath, host);
}

QString CodeParserTool::executeViewCodeItem(const QJsonObject& input, TmAgent::IToolPluginHost* host)
{
    QString filePath = input["file_path"].toString();
    QString itemName = input["item_name"].toString();
    qDebug() << "[CodeParserTool] 查看代码项:" << filePath << itemName;
    return viewCodeItem(filePath, itemName, host);
}

// ==================== 工具实现 ====================

QString CodeParserTool::viewFileOutline(const QString& filePath, TmAgent::IToolPluginHost* host)
{
    // 读取文件
    QString content = readFileContent(filePath);
    if (content.startsWith("错误:")) {
        return content;
    }

    // 使用宿主的代码解析服务
    QString error;
    QJsonObject ast = host->parseCode("cpp", content, &error);
    if (ast.isEmpty()) {
        return QString("错误: 解析失败 - %1").arg(error);
    }

    // 提取代码项
    QList<CodeItem> items;
    extractCodeItemsFromJson(ast, items, "", content);

    // 格式化输出
    QString result;
    result +("文件: %1\n").arg(filePath);
    result += QString("总行数: %1\n").arg(content.count('\n') + 1);
    result += "---\n";

    if (items.isEmpty()) {
        result += "未找到函数或类定义\n";
    } else {
        for (const CodeItem& item : items) {
            QString typeLabel = getTypeLabel(item.type);
            if (item.signature.isEmpty()) {
                result += QString("%1 %2 (%3-%4行)\n")
                              .arg(typeLabel)
                              .arg(item.name)
                              .arg(item.startLine)
                              .arg(item.endLine);
            } else {
                result += QString("%1 %2 (%3-%4行)\n    签名: %5\n")
                              .arg(typeLabel)
                              .arg(item.name)
                              .arg(item.startLine)
                              .arg(item.endLine)
                              .arg(item.signature);
            }
        }
        result += QString("\n共 %1 个代码项\n").arg(items.size());
    }

    return result;
}

Q CodeParserTool::viewCodeItem(const QString& filePath, const QString& itemName, TmAgent::IToolPluginHost* host)
{
    // 读取文件
    QString content = readFileContent(filePath);
    if (content.startsWith("错误:")) {
        return content;
    }

    // 使用宿主的代码解析服务
    QString error;
    QJsonObject ast = host->parseCode("cpp", content, &error);
    if (ast.isEmpty()) {
        return QString("错误: 解析失败 - %1").arg(error);
    }

    // 查找代码项
    QList<CodeItem> items;
    extractCodeItemsFromJs content);

    // 查找匹配的项
    CodeItem* foundItem = nullptr;
    for (CodeItem& item : items) {
        if (item.name == itemName) {
            foundItem = &item;
            break;
        }
    }

    if (!foundItem) {
        // 尝试模糊匹配
        for (CodeItem& item : items) {
            if (item.name.contains(itemName) || itemName.contains(item.name)) {
                foundItem = &item;
                break;
            }
        }
    }

    if (!foundItem) {
        QString availableItems;
        for (const CodeItem& item : items) {
            availableItems += QString("  - %1 (%2)\n").arg(item.name).arg(item.type);
        }
        return QString("错误: 未找到代码项 '%1'\n\n可用的代码项:\n%2")
            .arg(itemName)
            .arg(availableItems.isEmpty() ? "  (无)" : availableItems);
    }

    // 提取代码行
    QStringList lines = content.split('\n');
    int startIdx = static_cast<int>(foundItem->startLine) - 1;
    int endIdx = static_cast<int>(foundItem->endLine) - 1;

    // 边界检查
;
    endIdx = qMin(lines.size() - 1, endIdx);

    QString codeContent;
    for (int i = startIdx; i <= endIdx; ++i) {
        codeContent += QString("%1: %2\n").arg(i + 1, 4).arg(lines[i]);
    }

    QString result;
    result += QString("文件: %1\n").arg(filePath);
    result += QString("代码项: %1 (%2, 第 %3-%4 行)\n")
                  .arg(foundItem->name)
                  .arg(foundItem->type)
                  .arg(foundItem->startLine)
                  .arg(foundItem->endLine);
    result += "---\n";
    result += codeContent;

    return result;
}

// ==================== 私有辅助方法 ====================

QString CodeParserTool::readFileContent(const QString& filePath)
{
    QFile file(filePath);
    if (!file.exists()) {
        return QString("错误: 文件不存在 %1").arg(filePath);
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString("错误: 无法读取文件 %1").arg(filePath);
    }
    QTextStream in(&file);
    in.setCodec("UTF-8");
    QString content = in.readAll();
    file.close();
    return content;
}

QString CodeParserTool::getNodeText(const QJsonObject& node, const QString& code)
{
    if (!node.contains("start") || !node.contains("end")) {
        return QString();
    }
    
    int start = node["start"].toInt();
    int end = node["end"].toInt();
    
    if (start < 0 || end > code.length() || start >= end) {
        return QString();
    }
    
    return code.mid(start, end - start);
}

uint32_t CodeParserTool::getNodeStartLine(const QJsonObject& node)
{
    if (node.contains("startLin")) {
        return static_cast<uint32_t>(node["startLine"].toInt() + 1); // Convert to 1-based
    }
    return 0;
}

uint32_t CodeParserTool::getNodeEndLine(const QJsonObject& node)
{
    if (node.contains("endLine")) {
        return static_cast<uint32_t>(node["endLine"].toInt() + 1); // Convert to 1-based
    }
    return 0;
}

void CodeParserTool::extractCodeItemsFromJson(const QJsonObject& node, QList<CodeItem>& items, const QString& prefix, const QString& code)
{
    if (node.isEmpty()) {
        return;
    }

    QString nodeType = node["type"].toString();

    // 检查是否是我们关心的节点类型
    if (nodeType == "function_definition") {
        CodeItem item;
        item.type = "function";
        item.startLine = getNodeStartLine(node);
        item.endLine = getNodeEndLine(node);
        
        // 提取函数名 - 简化版本，从文本中提取
        QString text = getNodeText(node, code);
        if (!text.isEmpty()) {
            // 简单提取：找第一个 ( 之前的标识符
            int parenPos = text.indexOf('(');
            if (parenPos > 0) {
                QString beforeParen = text.left(parenPos).trimmed();
                QStringList parts = beforeParen.split(QRegExp("\\s+"));
                if (!parts.isEmpty()) {
                    item.name = parts.last();
                    if (!prefix.isEmpty()) {
                        item.name = prefix + "::" + item.name;
                    }
                }
                // 提取签名（第一行）
                item.signature = text.split('\n').first().trimmed();
            }
        }
        
     isEmpty()) {
            items.append(item);
        }
    } else if (nodeType == "class_specifier" || nodeType == "struct_specifier") {
        CodeItem item;
        item.type = (nodeType == "class_specifier") ? "class" : "struct";
        item.startLine = getNodeStartLine(node);
        item.endLine = getNodeEndLine(node);
        
        // 提取类名
        QString text = getNodeText(node, code);
        if (!text.isEmpty()) {
            // 简单提取：找 class/struct 关键字后的标识符
            QRegExp rx\\s+(\\w+)");
            if (rx.indexIn(text) != -1) {
                item.name = rx.cap(1);
                if (!prefix.isEmpty()) {
                    item.name = prefix + "::" + item.name;
                }
            }
        }
        
        if (!item.name.isEmpty()) {
            items.append(item);
            // 递归处理类成员
            QString newPrefix = prefix.isEmpty() ? item.name : prefix + "::" + item.name;
            if (node.contains("children")) {
 de["children"].toArray();
                for (const QJsonValue& child : children) {
                    extractCodeItemsFromJson(child.toObject(), items, newPrefix, code);
                }
            }
        }
        return;
    } else if (nodeType == "namespace_definition") {
        CodeItem item;
        item.type = "namespace";
        item.startLine = getNodeStartLine(node);
        item.endLine = getNodeEndLine(node);
        
        // 提取命名空间名
        QString text = getNodeText(node, code);
        if (!text.isEmpty()) {
            QRegExp rx("namespace\\s+(\\w+)");
            if (rx.indexIn(text) != -1) {
                item.name = rx.cap(1);
                if (!prefix.isEmpty()) {
                    item.name = prefix + "::" + item.name;
                }
            }
        }
        
        if (!item.name.isEmpty()) {
            items.append(item);
            // 递归处理命名空间内容
            QString newPrefix = prefix.isEmpty() ? item.name : prefix + "::" + item.name;
ntains("children")) {
                QJsonArray children = node["children"].toArray();
                for (const QJsonValue& child : children) {
                    extractCodeItemsFromJson(child.toObject(), items, newPrefix, code);
                }
            }
        }
        return;
    }

    // 递归子节点
    if (node.contains("children")) {
        QJsonArray children = node["children"].toArray();
        for (const QJsonValue& child : children) {
            extractCodeItemsFromJson(child.toO, items, prefix, code);
        }
    }
}

QString CodeParserTool::getTypeLabel(const QString& type)
{
    if (type == "function")
        return "[函数]";
    if (type == "method")
        return "[方法]";
    if (type == "class")
        return "[类]";
    if (type == "struct")
        return "[结构体]";
    if (type == "namespace")
        return "[命名空间]";
    return "[" + type + "]";
}
