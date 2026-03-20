#ifndef GOVERNANCESERVICE_H
#define GOVERNANCESERVICE_H

#include "AppFacade.h"
#include <memory>

class ApplicationServices;
class ConfigService;
class McpToolProvider;
class ModelFactory;
class RuntimeManager;
class ToolDispatcher;

class GovernanceService final : public IGovernanceService {
public:
    explicit GovernanceService(ApplicationServices& app);
    ~GovernanceService();

    void registerModelConfig(const ModelConfig& config) override;
    void setDefaultAgentConfig(const LLMConfig& config) override;
    LLMConfig defaultAgentConfig() const override;
    void applyConfigToAllRuntimes() override;
    void applyToolDispatcherToAllRuntimes() override;
    void applyMcpConfig(const QStringList& specs) override;
    QStringList loadMcpConfigSpecs() const override;
    bool saveMcpConfigSpecs(const QStringList& specs) const override;
    bool saveToolLoopPolicyObject(const QJsonObject& raw, QString* errOut = nullptr) const override;
    QString mcpConfigPath() const override;
    QString modelConfigPath() const override;
    QJsonObject defaultToolLoopPolicyObject() const override;
    QJsonObject normalizeToolLoopPolicyObject(const QJsonObject& raw) const override;
    QJsonObject loadToolLoopPolicyObject() const override;
    QStringList registeredModelConfigIds() const override;
    QStringList enabledProviderInstanceIds() const override;
    QString displayNameForProviderInstance(const QString& instanceId) const override;
    QList<AvailableModel> cachedModelsForProviderInstance(const QString& instanceId) const override;
    void fetchModelsForProviderInstanceAsync(const QString& instanceId) override;
    QStringList registeredToolNames() const override;

    void initialize(RuntimeManager* runtimeManager);
    void setModelConfigPathOverride(const QString& filePath);
    void loadConfig();

    ModelFactory* modelFactory() const;
    ToolDispatcher* toolDispatcher() const;
    McpToolProvider* mcpProvider() const;
    ConfigService* configService() const;

private:
    ApplicationServices& m_app;
    ModelFactory* m_modelFactory = nullptr;
    ToolDispatcher* m_toolDispatcher = nullptr;
    std::unique_ptr<McpToolProvider> m_mcpProvider;
    ConfigService* m_configService = nullptr;
};

#endif // GOVERNANCESERVICE_H
