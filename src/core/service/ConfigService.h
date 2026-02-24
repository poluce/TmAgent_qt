#ifndef CONFIGSERVICE_H
#define CONFIGSERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>

class ChatPersistenceService;
class McpToolProvider;
class ModelFactory;
class ToolDispatcher;
class RuntimeManager;

/**
 * @brief 配置管理服务
 *
 * 负责 MCP 配置、模型配置加载/保存、Tab 状态持久化。
 * 从 ChatService 提取，使 ChatService 专注于消息编排。
 */
class ConfigService : public QObject {
    Q_OBJECT
public:
    explicit ConfigService(QObject* parent = nullptr);
    ~ConfigService() override;

    // ---- 依赖注入 ----
    void setPersistence(ChatPersistenceService* persistence);
    void setModelFactory(ModelFactory* factory);
    void setMcpProvider(McpToolProvider* provider);
    void setToolDispatcher(ToolDispatcher* dispatcher);
    void setRuntimeManager(RuntimeManager* runtimeManager);

    // ---- MCP 配置 ----
    void applyMcpConfig(const QStringList& specs);
    QStringList loadMcpConfigSpecs() const;
    bool saveMcpConfigSpecs(const QStringList& specs) const;
    QString mcpConfigPath() const;

    // ---- 模型配置 ----
    QString modelConfigPath() const;
    void loadConfig();

    // ---- Tab 状态持久化 ----
    struct TabState {
        QStringList openAgentIds;
        QString activeIdentityId;
    };
    void saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId);
    TabState loadTabState() const;

signals:
    void configLoaded();

private:
    ChatPersistenceService* m_persistence = nullptr;
    ModelFactory* m_modelFactory = nullptr;
    McpToolProvider* m_mcpProvider = nullptr;
    ToolDispatcher* m_toolDispatcher = nullptr;
    RuntimeManager* m_runtimeManager = nullptr;
};

#endif // CONFIGSERVICE_H
