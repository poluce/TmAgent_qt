#include "TreeSitterParser.h"
#include <tree_sitter/api.h>
#include <cstdlib>
#include <cstring>

extern "C" {
    const TSLanguage* tree_sitter_cpp();
}

static TSNode toTSNode(const uint32_t context[4], const void* id, const void* tree) {
    TSNode node;
    memcpy(node.context, context, sizeof(node.context));
    node.id = id;
    node.tree = static_cast<const TSTree*>(tree);
    return node;
}

// SyntaxNode helpers
SyntaxNode SyntaxNode::fromInternal(const void* nodeData, const TreeSitterParser* parser) {
    return SyntaxNode(nodeData, parser);
}

SyntaxNode::SyntaxNode() 
    : m_context{0, 0, 0, 0}
    , m_id(nullptr)
    , m_tree(nullptr)
    , m_parser(nullptr) {
}

SyntaxNode::SyntaxNode(const void* nodeData, const TreeSitterParser* parser)
    : m_parser(parser) {
    if (nodeData) {
        const TSNode* node = static_cast<const TSNode*>(nodeData);
        memcpy(m_context, node->context, sizeof(m_context));
        m_id = node->id;
        m_tree = node->tree;
    } else {
        memset(m_context, 0, sizeof(m_context));
        m_id = nullptr;
        m_tree = nullptr;
    }
}

QString SyntaxNode::type() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    const char* t = ts_node_type(node);
    return t ? QString::fromUtf8(t) : QString();
}

QString SyntaxNode::text() const {
    if (!m_parser || isNull()) {
        return QString();
    }
    
    TSNode node = toTSNode(m_context, m_id, m_tree);
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    
    const QByteArray& source = m_parser->source();
    if (start >= static_cast<uint32_t>(source.size()) ||
        end > static_cast<uint32_t>(source.size()) ||
        start > end) {
        return QString();
    }
    
    return QString::fromUtf8(source.mid(static_cast<int>(start),
                                         static_cast<int>(end - start)));
}

bool SyntaxNode::isNull() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_is_null(node);
}

bool SyntaxNode::isNamed() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_is_named(node);
}

bool SyntaxNode::hasError() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_has_error(node);
}

bool SyntaxNode::isMissing() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_is_missing(node);
}

uint32_t SyntaxNode::startLine() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_start_point(node).row + 1;  // 1-based
}

uint32_t SyntaxNode::endLine() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_end_point(node).row + 1;
}

uint32_t SyntaxNode::startColumn() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_start_point(node).column;
}

uint32_t SyntaxNode::endColumn() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_end_point(node).column;
}

uint32_t SyntaxNode::startByte() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_start_byte(node);
}

uint32_t SyntaxNode::endByte() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_end_byte(node);
}

uint32_t SyntaxNode::childCount() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_child_count(node);
}

SyntaxNode SyntaxNode::child(uint32_t index) const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    TSNode child = ts_node_child(node, index);
    return SyntaxNode::fromInternal(&child, m_parser);
}

uint32_t SyntaxNode::namedChildCount() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    return ts_node_named_child_count(node);
}

SyntaxNode SyntaxNode::namedChild(uint32_t index) const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    TSNode child = ts_node_named_child(node, index);
    return SyntaxNode::fromInternal(&child, m_parser);
}

SyntaxNode SyntaxNode::childByFieldName(const QString& name) const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    QByteArray utf8 = name.toUtf8();
    TSNode child = ts_node_child_by_field_name(node, utf8.constData(),
                                                static_cast<uint32_t>(utf8.size()));
    return SyntaxNode::fromInternal(&child, m_parser);
}

SyntaxNode SyntaxNode::parent() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    TSNode p = ts_node_parent(node);
    return SyntaxNode::fromInternal(&p, m_parser);
}

SyntaxNode SyntaxNode::nextSibling() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    TSNode sibling = ts_node_next_sibling(node);
    return SyntaxNode::fromInternal(&sibling, m_parser);
}

SyntaxNode SyntaxNode::prevSibling() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    TSNode sibling = ts_node_prev_sibling(node);
    return SyntaxNode::fromInternal(&sibling, m_parser);
}

SyntaxNode SyntaxNode::nextNamedSibling() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    TSNode sibling = ts_node_next_named_sibling(node);
    return SyntaxNode::fromInternal(&sibling, m_parser);
}

SyntaxNode SyntaxNode::prevNamedSibling() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    TSNode sibling = ts_node_prev_named_sibling(node);
    return SyntaxNode::fromInternal(&sibling, m_parser);
}

QString SyntaxNode::sExpression() const {
    TSNode node = toTSNode(m_context, m_id, m_tree);
    if (ts_node_is_null(node))
        return QString();
    char* str = ts_node_string(node);
    if (!str)
        return QString();
    QString result = QString::fromUtf8(str);
    // Note: not freeing str to avoid MinGW crash
    return result;
}

TreeSitterParser::TreeSitterParser() {
    m_parser = ts_parser_new();
    if (!m_parser) {
        m_lastError = QStringLiteral("Failed to create parser");
        return;
    }
    if (!ts_parser_set_language(m_parser, tree_sitter_cpp()))
        m_lastError = QStringLiteral("Failed to set C++ language");
}

TreeSitterParser::~TreeSitterParser() {
    if (m_oldTree) {
        ts_tree_delete(m_oldTree);
    }
    if (m_tree) {
        ts_tree_delete(m_tree);
    }
    if (m_parser) {
        ts_parser_delete(m_parser);
    }
}

void TreeSitterParser::setTimeout(uint64_t microseconds) {
    Q_UNUSED(microseconds);
    m_lastError = QStringLiteral("setTimeout not supported in this version");
}

void TreeSitterParser::reset() {
    if (m_oldTree) {
        ts_tree_delete(m_oldTree);
        m_oldTree = nullptr;
    }
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
    m_hasEdit = false;
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

    TSInputEdit edit;
    edit.start_byte = startByte;
    edit.old_end_byte = oldEndByte;
    edit.new_end_byte = newEndByte;
    edit.start_point = {startRow > 0 ? startRow - 1 : 0, startCol};
    edit.old_end_point = {oldEndRow > 0 ? oldEndRow - 1 : 0, oldEndCol};
    edit.new_end_point = {newEndRow > 0 ? newEndRow - 1 : 0, newEndCol};

    ts_tree_edit(m_tree, &edit);
    m_hasEdit = true;
    m_lastError.clear();
}

bool TreeSitterParser::reparse(const QString& newSource) {
    return reparse(newSource.toUtf8());
}

bool TreeSitterParser::reparse(const QByteArray& newUtf8Source) {
    if (!m_parser) {
        m_lastError = QStringLiteral("Parser not initialized");
        return false;
    }

    // 释放上一次保存的旧树
    if (m_oldTree) {
        ts_tree_delete(m_oldTree);
        m_oldTree = nullptr;
    }

    TSTree* oldTree = m_tree;
    QByteArray oldSource = m_source;
    TSTree* treeForParsing = m_hasEdit ? oldTree : nullptr;

    m_source = newUtf8Source;
    m_tree = ts_parser_parse_string(m_parser, treeForParsing, m_source.constData(),
                                     static_cast<uint32_t>(m_source.size()));

    if (!m_tree) {
        m_tree = oldTree;
        m_source = oldSource;
        m_lastError = QStringLiteral("Reparsing failed");
        return false;
    }

    // 保存旧树用于 getChangedRanges()
    m_oldTree = oldTree;

    m_hasEdit = false;
    m_hasParsed = true;
    m_lastError.clear();
    return true;
}

SyntaxNode TreeSitterParser::rootNode() const {
    if (m_tree) {
        TSNode root = ts_tree_root_node(m_tree);
        return SyntaxNode::fromInternal(&root, this);
    }
    return SyntaxNode();
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

SyntaxNode TreeSitterParser::nodeAtPosition(uint32_t line, uint32_t column) const {
    if (!m_tree) {
        return SyntaxNode();
    }

    TSPoint point = {line > 0 ? line - 1 : 0, column};
    TSNode root = ts_tree_root_node(m_tree);
    TSNode node = ts_node_descendant_for_point_range(root, point, point);
    return SyntaxNode::fromInternal(&node, this);
}

QVector<ChangedRange> TreeSitterParser::getChangedRanges() const {
    QVector<ChangedRange> result;
    
    if (!m_oldTree || !m_tree) {
        return result;
    }
    
    uint32_t rangeCount = 0;
    TSRange* ranges = ts_tree_get_changed_ranges(m_oldTree, m_tree, &rangeCount);
    
    if (ranges && rangeCount > 0) {
        result.reserve(static_cast<int>(rangeCount));
        for (uint32_t i = 0; i < rangeCount; ++i) {
            ChangedRange cr;
            cr.startLine = ranges[i].start_point.row + 1;
            cr.startColumn = ranges[i].start_point.column;
            cr.endLine = ranges[i].end_point.row + 1;
            cr.endColumn = ranges[i].end_point.column;
            cr.startByte = ranges[i].start_byte;
            cr.endByte = ranges[i].end_byte;
            result.append(cr);
        }
        free(ranges);
    }
    
    return result;
}
