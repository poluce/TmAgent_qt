#ifndef MODEL_CONFIG_MANAGER_PAGE_H
#define MODEL_CONFIG_MANAGER_PAGE_H

#include "model_config_types.h"
#include <QVariantMap>
#include <QWidget>
#include <functional>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;

/**
 * @brief 库内部模型配置数据结构，替代外部 ModelConfig
 */
struct ModelConfigEntry {
    QString configId;
    QString providerId;
    QString displayName;
    QString baseUrl;
    QString apiKey;
    QString modelId;
    bool enabled = true;
    bool isDefault = false;
    QVariantMap extraFields;
};

/**
 * @brief 模型配置管理页面
 *
 * 左侧显示已导入的模型实例列表（带启用/禁用、默认标记），
 * 右侧是厂商选择器 + 配置表单，支持新建和编辑两种模式。
 */
class ModelConfigManagerPage : public QWidget {
    Q_OBJECT
public:
    enum class FormMode { CreateNew, EditExisting };
    enum class TestStatus { Idle, Testing, Success, Failed };

    // 回调类型：宿主注入数据加载逻辑，替代外部 ModelConfigLoader
    using ConfigListLoader = std::function<QList<ModelConfigEntry>(const QString& path)>;
    using SingleConfigLoader = std::function<ModelConfigEntry(const QString& path, const QString& configId)>;
    using DefaultConfigIdLoader = std::function<QString(const QString& path)>;

    explicit ModelConfigManagerPage(QWidget* parent = nullptr);

    void setProviders(const QList<ModelConfigProvider>& providers);
    void setYamlPath(const QString& yamlPath);
    void refreshConfigList();
    void setTestStatus(TestStatus status, const QString& message = QString());
    void setFieldError(const QString& fieldKey, const QString& message);
    void setFieldError(const QString& providerId, const QString& fieldKey, const QString& message);
    void setFieldOptions(const QString& providerId, const QString& fieldKey,
                         const QStringList& options, bool editable = true);
    void clearFieldErrors();
    void applyStyleSheet(const QString& styleSheet = QString());

    // 回调 setter：宿主注入数据加载逻辑
    void setConfigListLoader(const ConfigListLoader& loader);
    void setSingleConfigLoader(const SingleConfigLoader& loader);
    void setDefaultConfigIdLoader(const DefaultConfigIdLoader& loader);

    // 回调类型：Provider 标签推断、别名解析、configId 生成
    using ProviderTagInferrer = std::function<QString(const QString& providerId, const QString& baseUrl)>;
    using ProviderAliasResolver = std::function<QString(const QString& rawProviderId)>;
    using ConfigIdGenerator = std::function<QString(const QString& providerId, const QString& modelId)>;

    void setProviderTagInferrer(const ProviderTagInferrer& inferrer);
    void setProviderAliasResolver(const ProviderAliasResolver& resolver);
    void setConfigIdGenerator(const ConfigIdGenerator& generator);

signals:
    void configSaved(const QVariantMap& config);
    void configDeleted(const QString& configId);
    void defaultChanged(const QString& configId);
    void enabledToggled(const QString& configId, bool enabled);
    void testConnectionRequested(const QVariantMap& config);

private slots:
    void onConfigListItemClicked(QListWidgetItem* item);
    void onProviderButtonClicked(int index);
    void onSaveClicked();
    void onTestConnectionClicked();
    void onSetDefaultClicked();
    void onDeleteClicked();
    void onNewConfigClicked();

private:
    void setupUi();
    QWidget* createLeftPanel();
    QWidget* createRightPanel();
    QWidget* createFormWidget(const ModelConfigProvider& provider);
    QWidget* createConfigItemWidget(const QString& displayName, const QString& providerTag,
                                    bool isDefault, bool enabled);
    QVariantMap collectCurrentConfig() const;
    void switchToCreateMode();
    void switchToEditMode(const QString& configId);
    void autoGenerateConfigId();
    int providerIndexForId(const QString& providerId) const;
    QString inferProviderTag(const QString& provider, const QString& baseUrl) const;
    QString generatedConfigId() const;

    // 左侧面板
    QListWidget* m_configList = nullptr;
    QPushButton* m_setDefaultBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_newConfigBtn = nullptr;

    // 右侧面板
    QButtonGroup* m_providerGroup = nullptr;
    QStackedWidget* m_formStack = nullptr;
    QLineEdit* m_configIdEdit = nullptr;
    QLineEdit* m_displayNameEdit = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_testBtn = nullptr;
    QLabel* m_testStatusLabel = nullptr;

    // 状态
    FormMode m_formMode = FormMode::CreateNew;
    QString m_editingConfigId;
    QString m_yamlPath;
    QString m_defaultConfigId;
    QList<ModelConfigProvider> m_providers;

    // 复用 FieldWidgets 模式
    struct FieldWidgets {
        QString providerId;
        QHash<QString, QLineEdit*> inputs;
        QHash<QString, QComboBox*> combos;
        QHash<QString, QLabel*> errors;
    };
    QHash<int, FieldWidgets> m_fieldWidgetsMap;

    // 回调存储
    ConfigListLoader m_configListLoader;
    SingleConfigLoader m_singleConfigLoader;
    DefaultConfigIdLoader m_defaultConfigIdLoader;
    ProviderTagInferrer m_providerTagInferrer;
    ProviderAliasResolver m_providerAliasResolver;
    ConfigIdGenerator m_configIdGenerator;
};

#endif // MODEL_CONFIG_MANAGER_PAGE_H
