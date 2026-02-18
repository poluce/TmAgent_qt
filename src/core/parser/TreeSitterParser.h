#ifndef TREESITTERPARSER_H
#define TREESITTERPARSER_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <cstdint>

// 前向声明 tree-sitter 类型（仅在 .cpp 中包含 api.h）
struct TSNode;
struct TSTree;
struct TSParser;
struct TSLanguage;

class TreeSitterParser;

struct ChangedRange {
    uint32_t startLine;
    uint32_t startColumn;
    uint32_t endLine;
    uint32_t endColumn;
    uint32_t startByte;
    uint32_t endByte;
};

/**
 * @brief 语法节点封装（Qt 友好类型，完全隐藏 TSNode）
 *
 * @warning 生命周期依赖 TreeSitterParser：parse()/reparse()/reset() 后节点失效。
 */
class SyntaxNode {
public:
    SyntaxNode();

    QString type() const;
    QString text() const;
    bool isNull() const;
    bool isNamed() const;
    bool hasError() const;
    bool isMissing() const;

    uint32_t startLine() const; // 1-based
    uint32_t endLine() const;   // 1-based
    uint32_t startColumn() const;
    uint32_t endColumn() const;
    uint32_t startByte() const;
    uint32_t endByte() const;

    uint32_t childCount() const;
    SyntaxNode child(uint32_t index) const;
    uint32_t namedChildCount() const;
    SyntaxNode namedChild(uint32_t index) const;
    SyntaxNode childByFieldName(const QString& name) const;
    SyntaxNode parent() const;
    SyntaxNode nextSibling() const;
    SyntaxNode prevSibling() const;
    SyntaxNode nextNamedSibling() const;
    SyntaxNode prevNamedSibling() const;

    QString sExpression() const;

private:
    friend class TreeSitterParser;

    // 内部数据（与 TSNode 布局兼容）
    uint32_t m_context[4];
    const void* m_id;
    const void* m_tree;
    const TreeSitterParser* m_parser;

    SyntaxNode(const void* nodeData, const TreeSitterParser* parser);

    // 内部辅助
    static SyntaxNode fromInternal(const void* nodeData, const TreeSitterParser* parser);
};

/**
 * @brief Qt 风格的 tree-sitter 封装
 *
 * 提供代码解析、增量更新、节点遍历。对外完全隐藏 tree-sitter 底层类型。
 * 不可跨线程并发使用。SyntaxNode 在 parse/reparse/reset 后失效。
 */
class TreeSitterParser {
public:
    TreeSitterParser();
    ~TreeSitterParser();

    TreeSitterParser(const TreeSitterParser&) = delete;
    TreeSitterParser& operator=(const TreeSitterParser&) = delete;

    void setTimeout(uint64_t microseconds);
    void reset();

    bool parse(const QString& source);
    bool parse(const QByteArray& utf8Source);

    /// 通知编辑范围。行号 1-based（内部转 0-based），列号 UTF-8 字节偏移。
    /// 调用后应使用 reparse(newSource) 而非 parse()。
    void applyEdit(uint32_t startByte, uint32_t oldEndByte, uint32_t newEndByte, uint32_t startRow, uint32_t startCol, uint32_t oldEndRow, uint32_t oldEndCol, uint32_t newEndRow, uint32_t newEndCol);

    bool reparse(const QString& newSource);
    bool reparse(const QByteArray& newUtf8Source);

    SyntaxNode rootNode() const;
    bool hasTree() const;
    bool hasError() const;
    QString lastError() const;

    /// 按位置查找节点（line 1-based, column UTF-8 字节偏移）
    SyntaxNode nodeAtPosition(uint32_t line, uint32_t column) const;

    /// 获取上次 reparse 的变化区域（仅 reparse 后有效）
    QVector<ChangedRange> getChangedRanges() const;

    const QByteArray& source() const { return m_source; }

private:
    TSParser* m_parser = nullptr;
    TSTree* m_tree = nullptr;
    TSTree* m_oldTree = nullptr;
    QByteArray m_source;
    QString m_lastError;
    bool m_hasParsed = false;
    bool m_hasEdit = false;
};

#endif // TREESITTERPARSER_H
