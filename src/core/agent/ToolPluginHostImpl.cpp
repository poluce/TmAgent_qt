#include "ToolPluginHostImpl.h"
#include "ToolDispatcher.h"
#include "core/parser/TreeSitterParser.h"
#include "core/backend/BackendPluginManager.h"
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

ToolPluginHostImpl::ToolPluginHostImpl(ToolDispatcher* dispatcher, QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_parser(new TreeSitterParser())
{
    Q_ASSERT(m_dispatcher != nullptr);
}

ToolPluginHostImpl::~ToolPluginHostImpl()
{
    delete m_parser;
}

// ========== 查询服务 ==========

QStringList ToolPluginHostImpl::availableTeammateBackendIds() const
{
    // 委托给 ToolDispatcher，它会查询 BackendPluginManager
    return m_dispatcher->availableTeammateBackendIds();
}

QStringList ToolPluginHostImpl::availableTools() const
{
    // 从 ToolDispatcher 获取所有工具 schema 并提取名称
    const QList<Tool> schemas = m_dispatcher->getAllToolSchemas();
    QStringList toolNames;
    toolNames.reserve(schemas.size());
    
    for (const Tool& tool : schemas) {
        toolNames.append(tool.name);
    }
    
    // 按字母顺序排序
    toolNames.sort(Qt::CaseInsensitive);
    return toolNames;
}

// ========== 工具调用服务 ==========

TmAgent::ToolResult ToolPluginHostImpl::executeHostTool(const TmAgent::ToolCall& call)
{
    try {
        // 委托给 ToolDispatcher 执行工具
        return m_dispatcher->dispatch(call);
    } catch (const std::exception& e) {
        // 捕获 C++ 异常并转换为 ToolResult
        qCritical() << "[ToolPluginHostImpl] 工具执行异常:" << call.name << e.what();
        
        TmAgent::ToolResult result;
        result.rawContent = QString("内部错误：%1").arg(e.what());
        result.userSummary = "工具执行异常";
        result.success = false;
        result.data = QJsonObject{
            {"errorCode", "plugin_exception"},
            {"exception", e.what()}
        };
        return result;
    } catch (...) {
        // 捕获未知异常
        qCritical() << "[ToolPluginHostImpl] 工具执行未知异常:" << call.name;
        
        TmAgent::ToolResult result;
        result.rawContent = "内部错误：未知异常";
        result.userSummary = "工具执行异常";
        result.success = false;
        result.data = QJsonObject{{"errorCode", "unknown_exception"}};
        return result;
    }
}

// ========== 日志服务 ==========

void ToolPluginHostImpl::logDebug(const QString& pluginId, const QString& message)
{
    qDebug() << "[Plugin:" << pluginId << "]" << message;
}

void ToolPluginHostImpl::logInfo(const QString& pluginId, const QString& message)
{
    qInfo() << "[Plugin:" << pluginId << "]" << message;
}

void ToolPluginHostImpl::logWarning(const QString& pluginId, const QString& message)
{
    qWarning() << "[Plugin:" << pluginId << "]" << message;
}

void ToolPluginHostImpl::logError(const QString& pluginId, const QString& message)
{
    qCritical() << "[Plugin:" << pluginId << "]" << message;
}

// ========== 配置服务 ==========

QJsonObject ToolPluginHostImpl::getPluginConfig(const QString& pluginId) const
{
    QSettings settings;
    const QString key = QString("plugins/%1/config").arg(pluginId);
    
    // 读取配置字符串
    const QString configStr = settings.value(key).toString();
    if (configStr.isEmpty()) {
        return QJsonObject();
    }
    
    // 解析 JSON
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(configStr.toUtf8(), &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[ToolPluginHostImpl] 解析插件配置失败:" << pluginId
                   << parseError.errorString();
        return QJsonObject();
    }
    
    if (!doc.isObject()) {
        qWarning() << "[ToolPluginHostImpl] 插件配置不是 JSON 对象:" << pluginId;
        return QJsonObject();
    }
    
    return doc.object();
}

bool ToolPluginHostImpl::setPluginConfig(const QString& pluginId,
                                         const QJsonObject& config,
                                         QString* error)
{
    QSettings settings;
    const QString key = QString("plugins/%1/config").arg(pluginId);
    
    // 序列化为 JSON 字符串
    const QJsonDocument doc(config);
    const QString configStr = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    
    // 保存到 QSettings
    settings.setValue(key, configStr);
    
    // 立即同步到磁盘
    settings.sync();
    
    // 检查是否保存成功
    if (settings.status() != QSettings::NoError) {
        const QString errorMsg = QString("保存配置失败：QSettings 错误");
        if (error) {
            *error = errorMsg;
        }
        qWarning() << "[ToolPluginHostImpl]" << errorMsg << "pluginId:" << pluginId;
        return false;
    }
    
    qDebug() << "[ToolPluginHostImpl] 保存插件配置成功:" << pluginId;
    return true;
}

// ========== 文件服务 ==========

QString ToolPluginHostImpl::getPluginDataDir(const QString& pluginId) const
{
    // 获取应用数据目录
    const QString appDataDir = getAppDataDir();
    if (appDataDir.isEmpty()) {
        qWarning() << "[ToolPluginHostImpl] 无法获取应用数据目录";
        return QString();
    }
    
    // 构建插件数据目录路径
    const QString pluginDataDir = QDir(appDataDir).filePath(QString("plugins/%1").arg(pluginId));
    
    // 确保目录存在
    if (!ensureDirectoryExists(pluginDataDir)) {
        qWarning() << "[ToolPluginHostImpl] 无法创建插件数据目录:" << pluginDataDir;
        return QString();
    }
    
    return pluginDataDir;
}

QString ToolPluginHostImpl::getAppDataDir() const
{
    // 使用 QStandardPaths 获取应用数据目录
    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    
    if (dataDirs.isEmpty()) {
        qWarning() << "[ToolPluginHostImpl] QStandardPaths 返回空列表";
        return QString();
    }
    
    // 返回第一个路径
    return dataDirs.first();
}

// ========== 代码解析服务 ==========

QJsonObject ToolPluginHostImpl::parseCode(const QString& language,
                                          const QString& code,
                                          QString* error)
{
    // 检查语言支持
    if (language.toLower() != "cpp" && language.toLower() != "c++") {
        const QString errorMsg = QString("不支持的语言：%1（当前仅支持 C++）").arg(language);
        if (error) {
            *error = errorMsg;
        }
        qWarning() << "[ToolPluginHostImpl]" << errorMsg;
        return QJsonObject();
    }
    
    // 检查代码大小（限制 1MB）
    if (code.size() > 1024 * 1024) {
        const QString errorMsg = QString("代码过大：%1 字符（限制 1MB）").arg(code.size());
        if (error) {
            *error = errorMsg;
        }
        qWarning() << "[ToolPluginHostImpl]" << errorMsg;
        return QJsonObject();
    }
    
    // 解析代码
    const bool parseSuccess = m_parser->parse(code);
    if (!parseSuccess) {
        const QString errorMsg = QString("解析失败：%1").arg(m_parser->lastError());
        if (error) {
            *error = errorMsg;
        }
        qWarning() << "[ToolPluginHostImpl]" << errorMsg;
        return QJsonObject();
    }
    
    // 检查是否有语法错误
    if (m_parser->hasError()) {
        const QString errorMsg = "代码包含语法错误";
        if (error) {
            *error = errorMsg;
        }
        qWarning() << "[ToolPluginHostImpl]" << errorMsg;
        // 注意：即使有语法错误，仍然返回 AST（部分解析结果）
    }
    
    // 获取根节点
    const SyntaxNode rootNode = m_parser->rootNode();
    if (rootNode.isNull()) {
        const QString errorMsg = "无法获取根节点";
        if (error) {
            *error = errorMsg;
        }
        qWarning() << "[ToolPluginHostImpl]" << errorMsg;
        return QJsonObject();
    }
    
    // 转换为 JSON
    QJsonObject ast = syntaxNodeToJson(rootNode);
    ast["language"] = language;
    ast["hasError"] = m_parser->hasError();
    
    qDebug() << "[ToolPluginHostImpl] 代码解析成功:" << language
             << "节点数:" << rootNode.childCount();
    
    return ast;
}

// ========== 私有辅助方法 ==========

QJsonObject ToolPluginHostImpl::syntaxNodeToJson(const SyntaxNode& node, int maxDepth) const
{
    QJsonObject obj;
    
    // 检查递归深度
    if (maxDepth <= 0) {
        obj["error"] = "达到最大递归深度";
        return obj;
    }
    
    // 检查节点是否有效
    if (node.isNull()) {
        obj["null"] = true;
        return obj;
    }
    
    // 基本信息
    obj["type"] = node.type();
    obj["isNamed"] = node.isNamed();
    obj["hasError"] = node.hasError();
    obj["isMissing"] = node.isMissing();
    
    // 位置信息
    obj["startLine"] = static_cast<int>(node.startLine());
    obj["endLine"] = static_cast<int>(node.endLine());
    obj["startColumn"] = static_cast<int>(node.startColumn());
    obj["endColumn"] = static_cast<int>(node.endColumn());
    obj["startByte"] = static_cast<int>(node.startByte());
    obj["endByte"] = static_cast<int>(node.endByte());
    
    // 文本内容（仅对叶子节点或小节点）
    const QString text = node.text();
    if (text.length() <= 200) {
        obj["text"] = text;
    } else {
        obj["text"] = text.left(200) + "...";
        obj["textLength"] = text.length();
    }
    
    // 子节点（仅命名节点）
    const uint32_t namedChildCount = node.namedChildCount();
    if (namedChildCount > 0) {
        QJsonArray children;
        
        // 限制子节点数量（防止 JSON 过大）
        const uint32_t maxChildren = qMin(namedChildCount, static_cast<uint32_t>(100));
        for (uint32_t i = 0; i < maxChildren; ++i) {
            const SyntaxNode child = node.namedChild(i);
            children.append(syntaxNodeToJson(child, maxDepth - 1));
        }
        
        obj["children"] = children;
        obj["childCount"] = static_cast<int>(namedChildCount);
        
        if (namedChildCount > maxChildren) {
            obj["childrenTruncated"] = true;
        }
    }
    
    return obj;
}

bool ToolPluginHostImpl::ensureDirectoryExists(const QString& dirPath) const
{
    QDir dir(dirPath);
    if (dir.exists()) {
        return true;
    }
    
    // 递归创建目录
    if (!dir.mkpath(".")) {
        qWarning() << "[ToolPluginHostImpl] 创建目录失败:" << dirPath;
        return false;
    }
    
    qDebug() << "[ToolPluginHostImpl] 创建目录成功:" << dirPath;
    return true;
}
