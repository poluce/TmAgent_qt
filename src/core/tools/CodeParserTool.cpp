#include "CodeParserTool.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QStringList>
#include <QTextStream>

#include "core/parser/TreeSitterParser.h"

// ==================== 工具执行入口 ====================

QString CodeParserTool::executeViewFileOutline(const QJsonObject& input)
{
    QString filePath = input["file_path"].toString();
    qDebug() << "[CodeParserTool] 查看文件大纲:" << filePath;
    return viewFileOutline(filePath);
}

QString CodeParserTool::executeViewCodeItem(const QJsonObject& input)
{
    QString filePath = input["file_path"].toString();
    QString itemName = input["item_name"].toString();
    qDebug() << "[CodeParserTool] 查看代码项:" << filePath << itemName;
    return viewCodeItem(filePath, itemName);
}

// ==================== 工具实现 ====================

QString CodeParserTool::viewFileOutline(const QString& filePath)
{
    // 读取文件
    QString content = readFileContent(filePath);
    if (content.startsWith("错误:")) {
        return content;
    }

    // 解析文件
    TreeSitterParser parser;
    if (!parser.parse(content)) {
        return QString("错误: 解析失败 - %1").arg(parser.lastError());
    }

    // 提取代码项
    QList<CodeItem> items;
    SyntaxNode root = parser.rootNode();
    extractCodeItems(root, items, "");

    // 格式化输出
    QString result;
    result += QString("文件: %1\n").arg(filePath);
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

QString CodeParserTool::viewCodeItem(const QString& filePath, const QString& itemName)
{
    // 读取文件
    QString content = readFileContent(filePath);
    if (content.startsWith("错误:")) {
        return content;
    }

    // 解析文件
    TreeSitterParser parser;
    if (!parser.parse(content)) {
        return QString("错误: 解析失败 - %1").arg(parser.lastError());
    }

    // 查找代码项
    QList<CodeItem> items;
    SyntaxNode root = parser.rootNode();
    extractCodeItems(root, items, "");

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
    startIdx = qMax(0, startIdx);
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

void CodeParserTool::extractCodeItems(const SyntaxNode& node, QList<CodeItem>& items, const QString& prefix)
{
    if (node.isNull())
        return;

    QString nodeType = node.type();

    // 检查是否是我们关心的节点类型
    if (nodeType == "function_definition") {
        CodeItem item;
        item.type = "function";
        item.name = extractFunctionName(node, prefix);
        item.signature = extractFunctionSignature(node);
        item.startLine = node.startLine();
        item.endLine = node.endLine();
        if (!item.name.isEmpty()) {
            items.append(item);
        }
    } else if (nodeType == "class_specifier") {
        CodeItem item;
        item.type = "class";
        item.name = extractClassName(node);
        item.startLine = node.startLine();
        item.endLine = node.endLine();
        if (!item.name.isEmpty()) {
            items.append(item);
            // 继续处理类内部的方法
            QString newPrefix = prefix.isEmpty() ? item.name : prefix + "::" + item.name;
            extractClassMembers(node, items, newPrefix);
        }
        return; // 不再递归，已在 extractClassMembers 中处理
    } else if (nodeType == "struct_specifier") {
        CodeItem item;
        item.type = "struct";
        item.name = extractClassName(node); // 同样的提取逻辑
        item.startLine = node.startLine();
        item.endLine = node.endLine();
        if (!item.name.isEmpty()) {
            items.append(item);
            QString newPrefix = prefix.isEmpty() ? item.name : prefix + "::" + item.name;
            extractClassMembers(node, items, newPrefix);
        }
        return;
    } else if (nodeType == "namespace_definition") {
        QString nsName = extractNamespaceName(node);
        CodeItem item;
        item.type = "namespace";
        item.name = nsName;
        item.startLine = node.startLine();
        item.endLine = node.endLine();
        if (!item.name.isEmpty()) {
            items.append(item);
        }
        // 继续递归命名空间内部
        QString newPrefix = prefix.isEmpty() ? nsName : prefix + "::" + nsName;
        for (uint32_t i = 0; i < node.namedChildCount(); ++i) {
            extractCodeItems(node.namedChild(i), items, newPrefix);
        }
        return;
    }

    // 递归子节点
    for (uint32_t i = 0; i < node.namedChildCount(); ++i) {
        extractCodeItems(node.namedChild(i), items, prefix);
    }
}

QString CodeParserTool::extractFunctionName(const SyntaxNode& node, const QString& prefix)
{
    // 查找 declarator 子节点
    SyntaxNode declarator = node.childByFieldName("declarator");
    if (declarator.isNull()) {
        // 尝试遍历查找
        for (uint32_t i = 0; i < node.namedChildCount(); ++i) {
            SyntaxNode child = node.namedChild(i);
            if (child.type() == "function_declarator" || child.type() == "identifier" || child.type().contains("declarator")) {
                declarator = child;
                break;
            }
        }
    }

    if (declarator.isNull())
        return "";

    // 从 declarator 中提取名称
    QString name = extractIdentifierFromDeclarator(declarator);
    if (name.isEmpty())
        return "";

    return prefix.isEmpty() ? name : prefix + "::" + name;
}

QString CodeParserTool::extractIdentifierFromDeclarator(const SyntaxNode& node)
{
    QString nodeType = node.type();

    if (nodeType == "identifier") {
        return node.text();
    }
    if (nodeType == "field_identifier") {
        return node.text();
    }
    if (nodeType == "qualified_identifier") {
        return node.text();
    }
    if (nodeType == "destructor_name") {
        return node.text();
    }

    // 递归查找
    for (uint32_t i = 0; i < node.namedChildCount(); ++i) {
        QString result = extractIdentifierFromDeclarator(node.namedChild(i));
        if (!result.isEmpty()) {
            return result;
        }
    }

    return "";
}

QString CodeParserTool::extractFunctionSignature(const SyntaxNode& node)
{
    // 简化版：直接截取第一行作为签名
    QString text = node.text();
    int bracePos = text.indexOf('{');
    if (bracePos > 0) {
        return text.left(bracePos).trimmed();
    }
    // 对于没有函数体的声明
    int semiPos = text.indexOf(';');
    if (semiPos > 0) {
        return text.left(semiPos).trimmed();
    }
    return text.split('\n').first().trimmed();
}

QString CodeParserTool::extractClassName(const SyntaxNode& node)
{
    SyntaxNode nameNode = node.childByFieldName("name");
    if (!nameNode.isNull()) {
        return nameNode.text();
    }

    // 遍历查找 type_identifier
    for (uint32_t i = 0; i < node.namedChildCount(); ++i) {
        SyntaxNode child = node.namedChild(i);
        if (child.type() == "type_identifier") {
            return child.text();
        }
    }
    return "";
}

QString CodeParserTool::extractNamespaceName(const SyntaxNode& node)
{
    SyntaxNode nameNode = node.childByFieldName("name");
    if (!nameNode.isNull()) {
        return nameNode.text();
    }
    for (uint32_t i = 0; i < node.namedChildCount(); ++i) {
        SyntaxNode child = node.namedChild(i);
        if (child.type() == "identifier" || child.type() == "namespace_identifier") {
            return child.text();
        }
    }
    return "";
}

void CodeParserTool::extractClassMembers(const SyntaxNode& classNode, QList<CodeItem>& items, const QString& prefix)
{
    // 查找 field_declaration_list (类体)
    SyntaxNode body = classNode.childByFieldName("body");
    if (body.isNull()) {
        for (uint32_t i = 0; i < classNode.namedChildCount(); ++i) {
            SyntaxNode child = classNode.namedChild(i);
            if (child.type() == "field_declaration_list") {
                body = child;
                break;
            }
        }
    }

    if (body.isNull())
        return;

    // 遍历类体中的函数定义
    for (uint32_t i = 0; i < body.namedChildCount(); ++i) {
        SyntaxNode member = body.namedChild(i);
        if (member.type() == "function_definition") {
            CodeItem item;
            item.type = "method";
            item.name = extractFunctionName(member, prefix);
            item.signature = extractFunctionSignature(member);
            item.startLine = member.startLine();
            item.endLine = member.endLine();
            if (!item.name.isEmpty()) {
                items.append(item);
            }
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
