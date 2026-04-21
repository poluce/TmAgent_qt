#ifndef TOOLPLUGINHOSTIMPL_H
#define TOOLPLUGINHOSTIMPL_H

#include <tmagent/plugin/IToolPluginHost.h>
#include <QObject>

class ToolDispatcher;
class TreeSitterParser;

/**
 * @brief IToolPluginHost 接口的主应用实现
 * 
 * 此类实现了 SDK 定义的 IToolPluginHost 接口，为插件提供各种宿主服务：
 * - 查询服务（可用工具、后端）
 * - 工具调用服务（工具间调用）
 * - 日志服务（调试、信息、警告、错误）
 * - 配置服务（读取、保存插件配置）
 * - 文件服务（插件数据目录、应用数据目录）
 * - 代码解析服务（tree-sitter AST）
 * 
 * 使用方式：
 * @code
 * ToolPluginHostImpl* host = new ToolPluginHostImpl(dispatcher, parent);
 * IToolProvider* provider = plugin->createProvider(host, parent);
 * @endcode
 * 
 * @note 此类是线程安全的（通过 ToolDispatcher 的线程安全性保证）
 * @note 代码解析服务使用独立的 TreeSitterParser 实例，不可跨线程使用
 */
class ToolPluginHostImpl : public QObject, public TmAgent::IToolPluginHost {
    Q_OBJECT
    
public:
    /**
     * @brief 构造函数
     * 
     * @param dispatcher 工具调度器实例（用于工具调用和查询）
     * @param parent Qt 父对象（用于内存管理）
     */
    explicit ToolPluginHostImpl(ToolDispatcher* dispatcher, QObject* parent = nullptr);
    ~ToolPluginHostImpl() override;
    
    // ========== 查询服务 ==========
    
    /**
     * @brief 查询可用的队友后端 ID 列表
     * 
     * 实现方式：委托给 ToolDispatcher::availableTeammateBackendIds()
     * 
     * @return QStringList 后端 ID 列表
     */
    QStringList availableTeammateBackendIds() const override;
    
    /**
     * @brief 查询可用的工具名称列表
     * 
     * 实现方式：从 ToolDispatcher 获取所有工具 schema 并提取名称
     * 
     * @return QStringList 工具名称列表
     */
    QStringList availableTools() const override;
    
    // ========== 工具调用服务 ==========
    
    /**
     * @brief 执行主应用的工具（工具间调用）
     * 
     * 实现方式：委托给 ToolDispatcher::dispatch()
     * 
     * @param call 工具调用请求
     * @return ToolResult 工具执行结果
     * 
     * @note 此方法会捕获所有异常并转换为 ToolResult{success=false}
     */
    TmAgent::ToolResult executeHostTool(const TmAgent::ToolCall& call) override;
    
    // ========== 日志服务 ==========
    
    /**
     * @brief 记录调试日志
     * 
     * 实现方式：使用 qDebug() 并添加插件 ID 前缀
     * 
     * @param pluginId 插件 ID
     * @param message 日志消息
     */
    void logDebug(const QString& pluginId, const QString& message) override;
    
    /**
     * @brief 记录信息日志
     * 
     * 实现方式：使用 qInfo() 并添加插件 ID 前缀
     * 
     * @param pluginId 插件 ID
     * @param message 日志消息
     */
    void logInfo(const QString& pluginId, const QString& message) override;
    
    /**
     * @brief 记录警告日志
     * 
     * 实现方式：使用 qWarning() 并添加插件 ID 前缀
     * 
     * @param pluginId 插件 ID
     * @param message 日志消息
     */
    void logWarning(const QString& pluginId, const QString& message) override;
    
    /**
     * @brief 记录错误日志
     * 
     * 实现方式：使用 qCritical() 并添加插件 ID 前缀
     * 
     * @param pluginId 插件 ID
     * @param message 日志消息
     */
    void logError(const QString& pluginId, const QString& message) override;
    
    // ========== 配置服务 ==========
    
    /**
     * @brief 获取插件配置
     * 
     * 实现方式：从 QSettings 读取插件配置
     * 配置路径：plugins/{pluginId}/config
     * 
     * @param pluginId 插件 ID
     * @return QJsonObject 配置数据
     * 
     * @note 如果配置不存在，返回空对象
     */
    QJsonObject getPluginConfig(const QString& pluginId) const override;
    
    /**
     * @brief 保存插件配置
     * 
     * 实现方式：将配置保存到 QSettings
     * 配置路径：plugins/{pluginId}/config
     * 
     * @param pluginId 插件 ID
     * @param config 配置数据
     * @param error 错误信息（可选）
     * @return bool 保存是否成功
     * 
     * @note 配置会立即同步到磁盘
     */
    bool setPluginConfig(const QString& pluginId,
                        const QJsonObject& config,
                        QString* error = nullptr) override;
    
    // ========== 文件服务 ==========
    
    /**
     * @brief 获取插件专用数据目录
     * 
     * 实现方式：返回 {USER_DATA}/TmAgent/plugins/{pluginId}/
     * 如果目录不存在，自动创建
     * 
     * @param pluginId 插件 ID
     * @return QString 数据目录的绝对路径
     * 
     * @note 目录创建失败时返回空字符串
     */
    QString getPluginDataDir(const QString& pluginId) const override;
    
    /**
     * @brief 获取应用数据目录
     * 
     * 实现方式：返回 QStandardPaths::AppDataLocation
     * 
     * @return QString 应用数据目录的绝对路径
     */
    QString getAppDataDir() const override;
    
    // ========== 代码解析服务 ==========
    
    /**
     * @brief 解析代码为 AST（抽象语法树）
     * 
     * 实现方式：使用 TreeSitterParser 解析代码并转换为 JSON
     * 
     * @param language 编程语言（如 "cpp", "python", "javascript"）
     * @param code 要解析的代码内容
     * @param error 错误信息（可选）
     * @return QJsonObject AST 的 JSON 表示
     * 
     * @note 当前仅支持 C++ 语言
     * @note 如果解析失败，返回空对象并设置 error
     * @note 大文件解析可能耗时较长
     */
    QJsonObject parseCode(const QString& language,
                         const QString& code,
                         QString* error = nullptr) override;
    
private:
    /**
     * @brief 将 SyntaxNode 转换为 JSON 对象
     * 
     * 递归遍历语法树节点，生成 JSON 表示。
     * 
     * @param node 语法树节点
     * @param maxDepth 最大递归深度（防止栈溢出）
     * @return QJsonObject 节点的 JSON 表示
     */
    QJsonObject syntaxNodeToJson(const class SyntaxNode& node, int maxDepth = 50) const;
    
    /**
     * @brief 确保目录存在
     * 
     * 如果目录不存在，递归创建所有父目录。
     * 
     * @param dirPath 目录路径
     * @return bool 目录是否存在或创建成功
     */
    bool ensureDirectoryExists(const QString& dirPath) const;
    
    ToolDispatcher* m_dispatcher;           ///< 工具调度器（用于工具调用和查询）
    TreeSitterParser* m_parser;             ///< 代码解析器（用于 parseCode 服务）
};

#endif // TOOLPLUGINHOSTIMPL_H
