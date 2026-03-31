#ifndef CODEPARSERTOOL_H
#define CODEPARSERTOOL_H

#include "core/agent/ToolTypes.h"
#include <QJsonObject>
#include <QList>
#include <QString>

#include <cstdint>

class SyntaxNode;

/**
 * @brief 代码解析工具
 *
 * 提供代码结构分析能力，让 Agent 能够理解代码结构：
 *   - view_file_outline: 提取文件中的函数/类大纲
 *   - view_code_item: 按名称查看具体代码
 */
class CodeParserTool {
public:
    // ==================== 工具名称常量 ====================
    static constexpr const char* VIEW_FILE_OUTLINE = "view_file_outline";
    static constexpr const char* VIEW_CODE_ITEM = "view_code_item";
    static QList<Tool> toolSchemas();

    /**
     * @brief 代码项信息
     */
    struct CodeItem {
        QString type;      // 类型: function, class, struct, namespace
        QString name;      // 名称
        QString signature; // 签名（可选）
        uint32_t startLine;
        uint32_t endLine;
    };

    // ==================== 工具执行入口（接收 JSON 参数） ====================

    /**
     * @brief 执行 view_file_outline 工具
     * @param input JSON 参数 {file_path}
     */
    static QString executeViewFileOutline(const QJsonObject& input);

    /**
     * @brief 执行 view_code_item 工具
     * @param input JSON 参数 {file_path, item_name}
     */
    static QString executeViewCodeItem(const QJsonObject& input);

    // ==================== 工具实现 ====================

    /**
     * @brief 查看文件大纲
     * @param filePath 文件路径
     * @return 大纲信息
     */
    static QString viewFileOutline(const QString& filePath);

    /**
     * @brief 查看代码项
     * @param filePath 文件路径
     * @param itemName 代码项名称
     * @return 代码内容
     */
    static QString viewCodeItem(const QString& filePath, const QString& itemName);

private:
    /**
     * @brief 读取文件内容
     */
    static QString readFileContent(const QString& filePath);

    /**
     * @brief 递归提取代码项
     */
    static void extractCodeItems(const SyntaxNode& node, QList<CodeItem>& items, const QString& prefix);

    /**
     * @brief 提取函数名
     */
    static QString extractFunctionName(const SyntaxNode& node, const QString& prefix);

    /**
     * @brief 从 declarator 提取标识符
     */
    static QString extractIdentifierFromDeclarator(const SyntaxNode& node);

    /**
     * @brief 提取函数签名（返回类型 + 参数列表）
     */
    static QString extractFunctionSignature(const SyntaxNode& node);

    /**
     * @brief 提取类名
     */
    static QString extractClassName(const SyntaxNode& node);

    /**
     * @brief 提取命名空间名
     */
    static QString extractNamespaceName(const SyntaxNode& node);

    /**
     * @brief 提取类成员
     */
    static void extractClassMembers(const SyntaxNode& classNode, QList<CodeItem>& items, const QString& prefix);

    /**
     * @brief 获取类型标签
     */
    static QString getTypeLabel(const QString& type);
};

#endif // CODEPARSERTOOL_H
