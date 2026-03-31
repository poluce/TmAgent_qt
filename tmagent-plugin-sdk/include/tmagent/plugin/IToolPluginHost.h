#ifndef TMAGENT_ITOOLPLUGINHOST_H
#define TMAGENT_ITOOLPLUGINHOST_H

#include <tmagent/types/ToolTypes.h>
#include <QString>
#include <QStringList>
#include <QJsonObject>

namespace TmAgent {

/**
 * @brief 工具插件宿主回调接口
 * 
 * 此接口由主应用实现，提供给插件使用。插件可以通过此接口访问
 * 主应用提供的各种服务，包括工具调用、日志记录、配置管理等。
 * 
 * 插件在 createProvider() 时会收到此接口的指针，可以保存并在
 * 需要时调用。
 * 
 * 使用示例：
 * @code
 * class MyToolProvider : public QObject, public TmAgent::IToolProvider {
 * public:
 *     MyToolProvider(IToolPluginHost* host, QObject* parent)
 *         : QObject(parent), m_host(host) {}
 *     
 *     ToolResult execute(const ToolCall& call) override {
 *         // 记录日志
 *         m_host->logInfo("my_plugin", "Executing tool: " + call.name);
 *         
 *         // 调用其他工具
 *         ToolCall otherCall;
 *         otherCall.name = "read_file";
 *         otherCall.input = QJsonObject{{"path", "/tmp/data.txt"}};
 *         ToolResult result = m_host->executeHostTool(otherCall);
 *         
 *         return result;
 *     }
 * private:
 *     IToolPluginHost* m_host;
 * };
 * @endcode
 */
class IToolPluginHost {
public:
    virtual ~IToolPluginHost() = default;
    
    // ========== 查询服务 ==========
    
    /**
     * @brief 查询可用的队友后端 ID 列表
     * 
     * 返回当前已加载的所有队友后端的 ID。插件可以使用这些 ID
     * 来创建队友或检查特定后端是否可用。
     * 
     * @return QStringList 后端 ID 列表（如 ["codex", "tmagent"]）
     * 
     * @note 返回的列表可能随时间变化（后端可能被加载或卸载）
     */
    virtual QStringList availableTeammateBackendIds() const = 0;
    
    /**
     * @brief 查询可用的工具名称列表
     * 
     * 返回当前已注册的所有工具名称。插件可以使用此方法来检查
     * 特定工具是否可用，或列出所有可调用的工具。
     * 
     * @return QStringList 工具名称列表（如 ["read_file", "execute_shell"]）
     * 
     * @note 返回的列表包含所有插件提供的工具
     * @note 工具名称全局唯一
     */
    virtual QStringList availableTools() const = 0;
    
    // ========== 工具调用服务 ==========
    
    /**
     * @brief 执行主应用的工具（工具间调用）
     * 
     * 插件可以通过此方法调用主应用的其他工具，实现工具组合。
     * 主应用会将调用路由到对应的工具提供者。
     * 
     * @param call 工具调用请求
     * @return ToolResult 工具执行结果
     * 
     * @note 此方法是同步的，会阻塞直到工具执行完成
     * @note 如果工具不存在，返回 ToolResult{success=false, errorCode="unknown_tool"}
     * @note 避免循环调用（工具 A 调用工具 B，工具 B 又调用工具 A）
     * 
     * @see ToolCall
     * @see ToolResult
     */
    virtual ToolResult executeHostTool(const ToolCall& call) = 0;
    
    // ========== 日志服务 ==========
    
    /**
     * @brief 记录调试日志
     * 
     * 用于记录详细的调试信息，仅在调试模式下显示。
     * 
     * @param pluginId 插件 ID（通常使用 descriptor().pluginId）
     * @param message 日志消息
     * 
     * @note 日志会包含插件 ID 前缀，便于识别来源
     * @note 使用 Qt 标准日志系统（qDebug）
     */
    virtual void logDebug(const QString& pluginId, const QString& message) = 0;
    
    /**
     * @brief 记录信息日志
     * 
     * 用于记录一般性信息，如工具执行开始、完成等。
     * 
     * @param pluginId 插件 ID
     * @param message 日志消息
     * 
     * @note 使用 Qt 标准日志系统（qInfo）
     */
    virtual void logInfo(const QString& pluginId, const QString& message) = 0;
    
    /**
     * @brief 记录警告日志
     * 
     * 用于记录警告信息，如参数不推荐、性能问题等。
     * 
     * @param pluginId 插件 ID
     * @param message 日志消息
     * 
     * @note 使用 Qt 标准日志系统（qWarning）
     */
    virtual void logWarning(const QString& pluginId, const QString& message) = 0;
    
    /**
     * @brief 记录错误日志
     * 
     * 用于记录错误信息，如工具执行失败、资源不可用等。
     * 
     * @param pluginId 插件 ID
     * @param message 日志消息
     * 
     * @note 使用 Qt 标准日志系统（qCritical）
     */
    virtual void logError(const QString& pluginId, const QString& message) = 0;
    
    // ========== 配置服务 ==========
    
    /**
     * @brief 获取插件配置
     * 
     * 读取插件的持久化配置。配置由用户在 UI 中设置，或通过
     * setPluginConfig() 保存。
     * 
     * @param pluginId 插件 ID
     * @return QJsonObject 配置数据（JSON 对象格式）
     * 
     * @note 如果插件没有配置，返回空对象
     * @note 配置格式应符合 descriptor().configSchema
     */
    virtual QJsonObject getPluginConfig(const QString& pluginId) const = 0;
    
    /**
     * @brief 保存插件配置
     * 
     * 将插件配置持久化到磁盘。主应用会验证配置格式并保存。
     * 
     * @param pluginId 插件 ID
     * @param config 配置数据（JSON 对象格式）
     * @param error 如果保存失败，设置错误信息（可选）
     * @return bool 保存是否成功
     * 
     * @note 配置会在应用重启后保留
     * @note 如果配置不符合 configSchema，可能保存失败
     */
    virtual bool setPluginConfig(const QString& pluginId,
                                const QJsonObject& config,
                                QString* error = nullptr) = 0;
    
    // ========== 文件服务 ==========
    
    /**
     * @brief 获取插件专用数据目录
     * 
     * 返回插件可以用来存储数据的目录路径。每个插件有独立的目录，
     * 不会与其他插件冲突。
     * 
     * @param pluginId 插件 ID
     * @return QString 数据目录的绝对路径
     * 
     * @note 目录会自动创建（如果不存在）
     * @note 插件应将所有持久化数据存储在此目录中
     * @note 典型路径：{USER_DATA}/TmAgent/plugins/{pluginId}/
     */
    virtual QString getPluginDataDir(const QString& pluginId) const = 0;
    
    /**
     * @brief 获取应用数据目录
     * 
     * 返回主应用的数据目录路径。插件可以读取（但不应修改）
     * 应用的共享数据。
     * 
     * @return QString 应用数据目录的绝对路径
     * 
     * @note 典型路径：{USER_DATA}/TmAgent/
     * @note 插件不应直接写入此目录，应使用 getPluginDataDir()
     */
    virtual QString getAppDataDir() const = 0;
    
    // ========== 代码解析服务 ==========
    
    /**
     * @brief 解析代码为 AST（抽象语法树）
     * 
     * 使用 tree-sitter 解析代码，返回 AST 的 JSON 表示。
     * 插件可以使用此服务实现代码智能功能。
     * 
     * @param language 编程语言（如 "cpp", "python", "javascript"）
     * @param code 要解析的代码内容
     * @param error 如果解析失败，设置错误信息（可选）
     * @return QJsonObject AST 的 JSON 表示
     * 
     * @note 如果解析失败，返回空对象并设置 error
     * @note 支持的语言取决于主应用配置的 tree-sitter 解析器
     * @note 大文件解析可能耗时较长，建议限制代码大小
     * 
     * @see https://tree-sitter.github.io/tree-sitter/
     */
    virtual QJsonObject parseCode(const QString& language,
                                 const QString& code,
                                 QString* error = nullptr) = 0;
};

} // namespace TmAgent

#endif // TMAGENT_ITOOLPLUGINHOST_H
