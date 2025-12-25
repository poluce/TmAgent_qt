#ifndef TREESITTERPARSER_H
#define TREESITTERPARSER_H

#include <QString>
#include <QByteArray>
#include <tree_sitter/api.h>

/**
 * @brief Qt 风格的 tree-sitter 封装类
 *
 * 提供代码解析、增量更新、节点遍历等功能。
 *
 * @warning 线程安全：类实例不可跨线程并发使用。
 * @warning 节点生命周期：TSNode 在 parse/reparse/reset 后失效。
 */
class TreeSitterParser {
public:
    TreeSitterParser();
    ~TreeSitterParser();

    // 禁止拷贝
    TreeSitterParser(const TreeSitterParser&) = delete;
    TreeSitterParser& operator=(const TreeSitterParser&) = delete;

    // === 解析器管理 ===

    /**
     * @brief 设置解析语言（仅首次 parse 前可调用，reset 后可再调）
     * @return 成功 true，失败 false + lastError
     */
    bool setLanguage(const TSLanguage* language);

    /**
     * @brief 设置解析超时（微秒）
     * @note 暂不支持：tree-sitter 0.26 无此 API，调用后 lastError 会设置提示
     */
    void setTimeout(uint64_t microseconds);

    /**
     * @brief 重置所有状态，允许重新 setLanguage
     */
    void reset();

    // === 解析操作 ===

    bool parse(const QString& source);
    bool parse(const QByteArray& utf8Source);

    // === 增量解析 ===

    /**
     * @brief 通知编辑范围（立即调用 ts_tree_edit）
     *
     * @param startRow, oldEndRow, newEndRow 行号（1-based，内部转 0-based）
     * @param startCol, oldEndCol, newEndCol 列号（UTF-8 字节偏移，不转换）
     *
     * @warning 调用方必须确保 startByte/oldEndByte/newEndByte 与 row/col
     *          基于相同的 UTF-8 编码，否则树会损坏。
     * @note 若无树则忽略并设置 lastError
     * @note 调用后应使用 reparse(newSource) 而非 parse()，否则会丢失增量解析优势
     */
    void applyEdit(uint32_t startByte, uint32_t oldEndByte, uint32_t newEndByte,
                   uint32_t startRow, uint32_t startCol,
                   uint32_t oldEndRow, uint32_t oldEndCol,
                   uint32_t newEndRow, uint32_t newEndCol);

    /**
     * @brief 增量解析（用新源码替换）
     * @note 未调用 applyEdit 时退化为全量解析
     */
    bool reparse(const QString& newSource);
    bool reparse(const QByteArray& newUtf8Source);

    // === 语法树 ===

    TSNode rootNode() const;
    bool hasTree() const;

    /**
     * @brief 树中是否有语法错误
     */
    bool hasError() const;

    /**
     * @brief 最近 API 失败原因（parse 成功时清空）
     */
    QString lastError() const;

    // === 节点信息（静态方法，无状态） ===

    static QString nodeType(TSNode node);
    static uint32_t startLine(TSNode node);   ///< 1-based
    static uint32_t endLine(TSNode node);     ///< 1-based
    static uint32_t startColumn(TSNode node); ///< UTF-8 字节偏移
    static uint32_t endColumn(TSNode node);   ///< UTF-8 字节偏移
    static uint32_t startByte(TSNode node);
    static uint32_t endByte(TSNode node);
    static bool isNamed(TSNode node);
    static bool nodeHasError(TSNode node);
    static bool isMissing(TSNode node);
    static bool isNull(TSNode node);

    // === 节点文本 ===

    QString nodeText(TSNode node) const;

    // === 节点遍历 ===

    static uint32_t childCount(TSNode node);
    static TSNode child(TSNode node, uint32_t index);
    static uint32_t namedChildCount(TSNode node);
    static TSNode namedChild(TSNode node, uint32_t index);
    static TSNode childByFieldName(TSNode node, const QString& fieldName);
    static TSNode parent(TSNode node);
    static TSNode nextSibling(TSNode node);
    static TSNode prevSibling(TSNode node);
    static TSNode nextNamedSibling(TSNode node);
    static TSNode prevNamedSibling(TSNode node);

    // === 节点定位 ===

    /**
     * @brief 按位置查找节点
     * @param line 行号（1-based，内部转 0-based）
     * @param column 列号（UTF-8 字节偏移）
     */
    TSNode nodeAtPosition(uint32_t line, uint32_t column) const;

    // === 调试 ===

    /**
     * @brief 返回节点的 S-expression 表示
     * @note 内部自动释放 ts_node_string 分配的内存
     */
    static QString sExpression(TSNode node);

private:
    TSParser* m_parser = nullptr;
    TSTree* m_tree = nullptr;
    QByteArray m_source;
    QString m_lastError;
    bool m_hasParsed = false;
    bool m_hasEdit = false;  // 标记是否调用过 applyEdit
};

#endif // TREESITTERPARSER_H
