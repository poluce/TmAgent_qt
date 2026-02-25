#ifndef AGENTCREATEDIALOG_H
#define AGENTCREATEDIALOG_H

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>

class QComboBox;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

/**
 * @brief Agent 创建对话框
 *
 * 提供名称、岗位、头像、模型、性格、角色模板与系统提示词输入，用于创建新的 Agent Identity。
 */
class AgentCreateDialog : public QDialog {
    Q_OBJECT
public:
    explicit AgentCreateDialog(const QStringList& configIds, const QString& defaultConfigId = QString(), QWidget* parent = nullptr);

    QString agentName() const;
    QString roleName() const;
    QString avatarPath() const;
    QString configId() const;
    QString providerInstanceId() const;
    QString selectedModelId() const;
    QString systemPrompt() const;
    bool delegationEnabled() const;

    // 新路径：设置接入点列表和模型缓存
    struct ProviderEntry {
        QString instanceId;
        QString displayName;
    };
    struct ModelEntry {
        QString modelId;
        QString displayName;
    };
    void setProviderEntries(const QList<ProviderEntry>& entries, const QString& defaultInstanceId);
    void setModelEntries(const QString& instanceId, const QList<ModelEntry>& models, const QString& defaultModelId);

private:
    struct PromptTemplateItem {
        QString id;
        QString name;
        QString content;
    };

    struct PersonalityItem {
        QString id;
        QString name;
        QString instruction;
    };

    struct RolePresetItem {
        QString id;
        QString name;
        QString title;
        QString suggestedName;
        QString defaultPromptId;
        QString defaultPersonalityId;
        QString avatarPath;
    };

    void loadPresetConfig();
    void applyRolePreset(const QString& roleId);
    void refreshAvatarPreview();
    void applyPromptComposition(bool forceOverwrite);
    QString composePrompt() const;
    QString resolveResourcePath(const QString& maybeRelativePath) const;
    void chooseAvatar();

    // 新路径：接入点 + 模型缓存
    QHash<QString, QList<ModelEntry>> m_modelEntriesCache;

signals:
    void providerChanged(const QString& instanceId);

private:
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_roleCombo = nullptr;
    QToolButton* m_avatarButton = nullptr;
    QComboBox* m_promptTemplateCombo = nullptr;
    QComboBox* m_personalityCombo = nullptr;
    QCheckBox* m_delegateCheck = nullptr;
    QPushButton* m_applyPromptBtn = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QComboBox* m_providerCombo = nullptr;
    QComboBox* m_modelSelectCombo = nullptr;
    QPlainTextEdit* m_promptEdit = nullptr;
    QString m_avatarPath;
    QString m_configDir;
    bool m_avatarCustom = false;
    bool m_promptEdited = false;
    bool m_updatingPrompt = false;

    QHash<QString, PromptTemplateItem> m_promptTemplates;
    QHash<QString, PersonalityItem> m_personalities;
    QHash<QString, RolePresetItem> m_rolePresets;
};

#endif // AGENTCREATEDIALOG_H
