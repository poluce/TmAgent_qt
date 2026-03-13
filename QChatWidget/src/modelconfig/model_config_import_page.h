#ifndef MODEL_CONFIG_IMPORT_PAGE_H
#define MODEL_CONFIG_IMPORT_PAGE_H

#include <QVariantMap>
#include <QWidget>
#include <QStringList>
#include <functional>

class QListWidget;
class QStackedWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QFormLayout;
class QLabel;

/**
 * @brief 厂商字段定义
 */
struct ModelConfigField {
    QString key;          // 数据键名 (如 "apiKey")
    QString label;        // 界面显示的标签 (如 "API Key")
    QString placeholder;  // 占位符
    QString defaultValue; // 默认值
    bool isPassword;      // 是否为密码输入
    bool isRequired;      // 是否必填

    ModelConfigField(const QString& k, const QString& l, const QString& p = "", const QString& d = "", bool pass = false, bool req = true)
        : key(k), label(l), placeholder(p), defaultValue(d), isPassword(pass), isRequired(req) { }
};

/**
 * @brief 厂商完整配置
 */
struct ModelConfigProvider {
    QString id;                     // 唯一标识 (如 "openai")
    QString name;                   // 显示名称 (如 "OpenAI")
    QString description;            // 描述
    QList<ModelConfigField> fields; // 包含的字段列表

    ModelConfigProvider() = default;
    ModelConfigProvider(const QString& i, const QString& n, const QString& d = "")
        : id(i), name(n), description(d) { }
};

class ModelConfigImportPage : public QWidget {
    Q_OBJECT
public:
    enum class TestStatus {
        Idle,
        Testing,
        Success,
        Failed
    };
    Q_ENUM(TestStatus)

    explicit ModelConfigImportPage(QWidget* parent = nullptr);

    /**
     * @brief 动态添加一个模型厂商
     */
    void addProvider(const ModelConfigProvider& provider);

    /**
     * @brief 批量设置厂商列表（会清空现有列表）
     */
    void setProviders(const QList<ModelConfigProvider>& providers);

    /**
     * @brief 设置保存的配置数据（用于回显）
     *
     * 支持 configId / enabled 字段回显。
     */
    void setConfigData(const QVariantMap& data);

    /**
     * @brief 获取当前表单的配置
     */
    QVariantMap configData() const;

    /**
     * @brief 应用自定义样式（如果不传则加载内置默认样式）
     */
    void applyStyleSheet(const QString& styleSheet = QString());

    /**
     * @brief UI 状态控制（无业务逻辑）
     */
    void setTestStatus(TestStatus status, const QString& message = QString());
    void clearFieldErrors();
    void setFieldError(const QString& fieldKey, const QString& message);
    void setFieldError(const QString& providerId, const QString& fieldKey, const QString& message);
    void setFieldOptions(const QString& providerId, const QString& fieldKey, const QStringList& options, bool editable = true);
    bool isDirty() const { return m_dirty; }

    // 回调类型：Provider 别名解析、configId 生成
    using ProviderAliasResolver = std::function<QString(const QString& rawProviderId)>;
    using ConfigIdGenerator = std::function<QString(const QString& providerId, const QString& modelId)>;

    void setProviderAliasResolver(const ProviderAliasResolver& resolver);
    void setConfigIdGenerator(const ConfigIdGenerator& generator);

signals:
    void importRequested(const QVariantMap& config);
    void cancelled();
    void testConnectionRequested(const QVariantMap& config);
    void importFromFileRequested();
    void exportRequested(const QVariantMap& config);
    void resetRequested(const QString& providerId);
    void dirtyChanged(bool dirty);

private slots:
    void onImportClicked();
    void onTestConnectionClicked();
    void onImportFromFileClicked();
    void onExportClicked();
    void onResetClicked();

private:
    void setupUi();
    QWidget* createFormWidget(const ModelConfigProvider& provider);
    QVariantMap collectCurrentConfig() const;
    void updateListWidgetWidth(); // 动态计算并更新列表宽度
    void setDirty(bool dirty);
    int providerIndexForId(const QString& providerId) const;
    void autoGenerateConfigId();
    QString generatedConfigId() const;

    QListWidget* m_providerList = nullptr;
    QStackedWidget* m_detailStack = nullptr;

    // 存储各个厂商对应的输入框容器，Key 为 StackedWidget 的索引
    struct FieldWidgets {
        QString providerId;
        QHash<QString, QLineEdit*> inputs; // Key 是字段的 key
        QHash<QString, QComboBox*> combos; // Key 是字段的 key
        QHash<QString, QLabel*> errors;    // Key 是字段的 key
    };
    QHash<int, FieldWidgets> m_fieldWidgetsMap;
    QList<ModelConfigProvider> m_providers;

    // 通用字段（所有厂商共享，位于表单顶部）
    QLineEdit* m_displayNameEdit = nullptr;
    QLineEdit* m_configIdEdit = nullptr;
    QCheckBox* m_enabledCheck = nullptr;

    QPushButton* m_importBtn = nullptr;
    QPushButton* m_testBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_importFileBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QLabel* m_testStatusLabel = nullptr;
    TestStatus m_testStatus = TestStatus::Idle;
    bool m_dirty = false;

    // 回调存储
    ProviderAliasResolver m_providerAliasResolver;
    ConfigIdGenerator m_configIdGenerator;
};

#endif // MODEL_CONFIG_IMPORT_PAGE_H
