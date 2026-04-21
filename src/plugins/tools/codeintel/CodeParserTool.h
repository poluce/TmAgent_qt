#ifndef CODEPARSERTOOL_H
#define CODEPARSERTOOL_H

#include <tmagent/types/ToolTypes.h>
#include <tmagent/plugin/IToolPluginHost.h>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <cstdint>

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
    static QList<TmAgent::Tool> toolSchemas();

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
     * @param host 宿主接口（用于代码解析服务）
     */
    static QString executeViewFileOutline(const QJsonObject& input, TmAgent::IToolPluginHost* host);

    /**
     * @brief 执行 view_code_item 工具
     * @param input JSON 参数 {file_path, item_name}
     * @param host 宿主接口（用于代码解析服务）
     */
    static QString executeViewCodeItem(const QJsonObject& input, TmAgent::IToolPluginHost* host);

    // ==================== 工具实现 ====================

    /**
     * @brief 查看文件大纲
     * @param filePath 文件路径
     * @param host 宿主接口
     * @return 大纲信息
     */
    static QString viewFileOutline(const QString& filePath, TmAgent::IToolPluginHost* host);

    /**
     * @brief 查看代码项
     * @param filePath 文件路径
     * @param itemName 代码项名称
     * @param host 宿主接口
     * @return 代码内容
     */
    static QString viewCodeItem(const QString& filePath, const QString& itemName, TmAgent::IToolPluginHost* host);

private:
    /**
     * @brief 读取文件内容
     */
    static QString readFileContent(const QString& filePath);

    /**
     * @brief 从 AST JSON 提取代码项
     */
    static void extractCodeItemsFromJson(const QJsonObject& node, QList<CodeItem>& items, const QString& prefix, const QString& code);

    /**
     * @brief 获取类型标签
     */
    static QString getTypeLabel(const QString& type);
    
    /**
     * @brief 从 JSON 节点获取文本
     */
    static QString getNodeText(const QJsonObject& node, const QString& code);
    
    /**
     * @brief 从 JSON 节点获取行号
     */
    static uint32_t getNodeStartLine(const QJsonObject& node);
    static uint32_t getNodeEndLine(const QJsonObject& node);
};

#endif // CODEPARSERTOOL_H
