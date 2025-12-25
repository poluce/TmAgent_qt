#include "TreeSitterParser.h"
#include <cstdlib>

// tree-sitter-cpp 语言声明
extern "C" {
    const TSLanguage* tree_sitter_cpp();
}

TreeSitterParser::TreeSitterParser() {
    m_parser = ts_parser_new();
    if (!m_parser) {
        m_lastError = QStringLiteral("Failed to create parser");
        return;
    }

    // 默认设置 C++ 语言
    if (!ts_parser_set_language(m_parser, tree_sitter_cpp())) {
        m_lastError = QStringLiteral("Failed to set C++ language");
    }
}

TreeSitterParser::~TreeSitterParser() {
    if (m_tree) {
        ts_tree_delete(m_tree);
    }
    if (m_parser) {
        ts_parser_delete(m_parser);
    }
}

bool TreeSitterParser::setLanguage(const TSLanguage* language) {
    if (!m_parser) {
        m_lastError = QStringLiteral("Parser not initialized");
        return false;
    }
    if (m_hasParsed) {
        m_lastError = QStringLiteral("Cannot change language after parsing (call reset() first)");
        return false;
    }
    if (!ts_parser_set_language(m_parser, language)) {
        m_lastError = QStringLiteral("Failed to set language (ABI version mismatch?)");
        return false;
    }
    m_lastError.clear();  // 成功时清空错误信息
    return true;
}

void TreeSitterParser::setTimeout(uint64_t microseconds) {
    Q_UNUSED(microseconds);
    // 暂不支持：tree-sitter 0.26 无 ts_parser_set_timeout_micros
    // 若需超时控制，可使用 TSParseOptions + progress_callback 实现
    m_lastError = QStringLiteral("setTimeout not supported in this version");
}

void TreeSitterParser::reset() {
    if (m_tree) {
        ts_tree_delete(m_tree);
        m_tree = nullptr;
    }
    m_source.clear();
    m_lastError.clear();
    m_hasParsed = false;
    m_hasEdit = false;
    if (m_parser) {
        ts_parser_reset(m_parser);
    }
}

bool TreeSitterParser::parse(const QString& source) {
    return parse(source.toUtf8());
}

bool TreeSitterParser::parse(const QByteArray& utf8Source) {
    if (!m_parser) {
        m_lastError = QStringLiteral("Parser not initialized");
        return false;
    }

    // 释放旧树
    if (m_tree) {
        ts_tree_delete(m_tree);
        m_tree = nullptr;
    }

    m_source = utf8Source;
    m_tree = ts_parser_parse_string(m_parser, nullptr, m_source.constData(),
                                     static_cast<uint32_t>(m_source.size()));

    if (!m_tree) {
        m_lastError = QStringLiteral("Parsing failed");
        return false;
    }

    m_hasParsed = true;
    m_hasEdit = false;  // 新解析后重置编辑标记
    m_lastError.clear();
    return true;
}

void TreeSitterParser::applyEdit(uint32_t startByte, uint32_t oldEndByte, uint32_t newEndByte,
                                  uint32_t startRow, uint32_t startCol,
                                  uint32_t oldEndRow, uint32_t oldEndCol,
                                  uint32_t newEndRow, uint32_t newEndCol) {
    if (!m_tree) {
        m_lastError = QStringLiteral("No tree to edit");
        return;
    }

    // 输入是 1-based 行号，转换为 0-based
    TSInputEdit edit;
    edit.start_byte = startByte;
    edit.old_end_byte = oldEndByte;
    edit.new_end_byte = newEndByte;
    edit.start_point = {startRow > 0 ? startRow - 1 : 0, startCol};
    edit.old_end_point = {oldEndRow > 0 ? oldEndRow - 1 : 0, oldEndCol};
    edit.new_end_point = {newEndRow > 0 ? newEndRow - 1 : 0, newEndCol};

    ts_tree_edit(m_tree, &edit);
    m_hasEdit = true;
    m_lastError.clear();  // 成功时清空错误信息
}

bool TreeSitterParser::reparse(const QString& newSource) {
    return reparse(newSource.toUtf8());
}

bool TreeSitterParser::reparse(const QByteArray& newUtf8Source) {
    if (!m_parser) {
        m_lastError = QStringLiteral("Parser not initialized");
        return false;
    }

    // 保存旧树和旧源码用于失败回滚
    TSTree* oldTree = m_tree;
    QByteArray oldSource = m_source;

    // 若未调用 applyEdit，传 nullptr 实现真正的全量解析
    TSTree* treeForParsing = m_hasEdit ? oldTree : nullptr;

    m_source = newUtf8Source;
    m_tree = ts_parser_parse_string(m_parser, treeForParsing, m_source.constData(),
                                     static_cast<uint32_t>(m_source.size()));

    if (!m_tree) {
        // 解析失败，回滚到旧树
        m_tree = oldTree;
        m_source = oldSource;
        m_lastError = QStringLiteral("Reparsing failed");
        return false;
    }

    // 成功后释放旧树
    if (oldTree) {
        ts_tree_delete(oldTree);
    }

    m_hasEdit = false;  // 重置编辑标记
    m_hasParsed = true;
    m_lastError.clear();
    return true;
}

TSNode TreeSitterParser::rootNode() const {
    if (m_tree) {
        return ts_tree_root_node(m_tree);
    }
    return TSNode{};  // null node
}

bool TreeSitterParser::hasTree() const {
    return m_tree != nullptr;
}

bool TreeSitterParser::hasError() const {
    if (!m_tree) {
        return false;
    }
    return ts_node_has_error(ts_tree_root_node(m_tree));
}

QString TreeSitterParser::lastError() const {
    return m_lastError;
}

// === 节点信息（静态方法） ===

QString TreeSitterParser::nodeType(TSNode node) {
    const char* type = ts_node_type(node);
    return type ? QString::fromUtf8(type) : QString();
}

uint32_t TreeSitterParser::startLine(TSNode node) {
    return ts_node_start_point(node).row + 1;  // 0-based 转 1-based
}

uint32_t TreeSitterParser::endLine(TSNode node) {
    return ts_node_end_point(node).row + 1;
}

uint32_t TreeSitterParser::startColumn(TSNode node) {
    return ts_node_start_point(node).column;
}

uint32_t TreeSitterParser::endColumn(TSNode node) {
    return ts_node_end_point(node).column;
}

uint32_t TreeSitterParser::startByte(TSNode node) {
    return ts_node_start_byte(node);
}

uint32_t TreeSitterParser::endByte(TSNode node) {
    return ts_node_end_byte(node);
}

bool TreeSitterParser::isNamed(TSNode node) {
    return ts_node_is_named(node);
}

bool TreeSitterParser::nodeHasError(TSNode node) {
    return ts_node_has_error(node);
}

bool TreeSitterParser::isMissing(TSNode node) {
    return ts_node_is_missing(node);
}

bool TreeSitterParser::isNull(TSNode node) {
    return ts_node_is_null(node);
}

QString TreeSitterParser::nodeText(TSNode node) const {
    if (m_source.isEmpty() || ts_node_is_null(node)) {
        return QString();
    }

    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);

    if (start >= static_cast<uint32_t>(m_source.size()) ||
        end > static_cast<uint32_t>(m_source.size()) ||
        start > end) {
        return QString();
    }

    return QString::fromUtf8(m_source.mid(static_cast<int>(start),
                                           static_cast<int>(end - start)));
}

// === 节点遍历 ===

uint32_t TreeSitterParser::childCount(TSNode node) {
    return ts_node_child_count(node);
}

TSNode TreeSitterParser::child(TSNode node, uint32_t index) {
    return ts_node_child(node, index);
}

uint32_t TreeSitterParser::namedChildCount(TSNode node) {
    return ts_node_named_child_count(node);
}

TSNode TreeSitterParser::namedChild(TSNode node, uint32_t index) {
    return ts_node_named_child(node, index);
}

TSNode TreeSitterParser::childByFieldName(TSNode node, const QString& fieldName) {
    QByteArray utf8 = fieldName.toUtf8();
    return ts_node_child_by_field_name(node, utf8.constData(),
                                        static_cast<uint32_t>(utf8.size()));
}

TSNode TreeSitterParser::parent(TSNode node) {
    return ts_node_parent(node);
}

TSNode TreeSitterParser::nextSibling(TSNode node) {
    return ts_node_next_sibling(node);
}

TSNode TreeSitterParser::prevSibling(TSNode node) {
    return ts_node_prev_sibling(node);
}

TSNode TreeSitterParser::nextNamedSibling(TSNode node) {
    return ts_node_next_named_sibling(node);
}

TSNode TreeSitterParser::prevNamedSibling(TSNode node) {
    return ts_node_prev_named_sibling(node);
}

TSNode TreeSitterParser::nodeAtPosition(uint32_t line, uint32_t column) const {
    if (!m_tree) {
        return TSNode{};
    }

    // 1-based 转 0-based
    TSPoint point = {line > 0 ? line - 1 : 0, column};
    TSNode root = ts_tree_root_node(m_tree);
    return ts_node_descendant_for_point_range(root, point, point);
}

QString TreeSitterParser::sExpression(TSNode node) {
    char* str = ts_node_string(node);
    if (!str) {
        return QString();
    }
    QString result = QString::fromUtf8(str);
    free(str);  // ts_node_string 使用 malloc 分配
    return result;
}
