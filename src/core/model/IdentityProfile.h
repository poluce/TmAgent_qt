#ifndef IDENTITYPROFILE_H
#define IDENTITYPROFILE_H

#include "llm/LLMTypes.h"
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief Agent 的角色配置（岗位描述、模型配置、工具权限等）
 *
 * 只有 Agent 类型的 Identity 才持有 IdentityProfile。
 * 继承 QObject 以利用 parent-child 自动销毁和信号槽。
 */
class IdentityProfile : public QObject {
    Q_OBJECT
public:
    explicit IdentityProfile(QObject* parent = nullptr);
    IdentityProfile(const IdentityProfile& other, QObject* parent = nullptr);

    // ---- 角色描述 ----
    QString description() const;
    void setDescription(const QString& desc);

    // ---- 系统提示词 ----
    QString systemPrompt() const;
    void setSystemPrompt(const QString& prompt);

    // ---- 模型配置 ----
    LLMConfig llmConfig() const;
    void setLlmConfig(const LLMConfig& config);

    // ---- 工具权限 ----
    QStringList allowedTools() const;
    void setAllowedTools(const QStringList& tools);
    bool delegateEnabled() const;
    void setDelegateEnabled(bool enabled);

    // ---- 递归深度 ----
    int recursionDepth() const;
    void setRecursionDepth(int depth);

signals:
    void changed();

private:
    QString m_description;
    QString m_systemPrompt;
    LLMConfig m_llmConfig;
    QStringList m_allowedTools;
    bool m_delegateEnabled = true;
    int m_recursionDepth = 3;
};

#endif // IDENTITYPROFILE_H
