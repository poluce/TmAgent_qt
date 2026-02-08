#include "IdentityProfile.h"

IdentityProfile::IdentityProfile(QObject* parent)
    : QObject(parent)
{
}

IdentityProfile::IdentityProfile(const IdentityProfile& other, QObject* parent)
    : QObject(parent)
    , m_description(other.m_description)
    , m_systemPrompt(other.m_systemPrompt)
    , m_llmConfig(other.m_llmConfig)
    , m_allowedTools(other.m_allowedTools)
    , m_recursionDepth(other.m_recursionDepth)
{
}

QString IdentityProfile::description() const { return m_description; }
void IdentityProfile::setDescription(const QString& desc)
{
    if (m_description != desc) {
        m_description = desc;
        emit changed();
    }
}

QString IdentityProfile::systemPrompt() const { return m_systemPrompt; }
void IdentityProfile::setSystemPrompt(const QString& prompt)
{
    if (m_systemPrompt != prompt) {
        m_systemPrompt = prompt;
        emit changed();
    }
}

LLMConfig IdentityProfile::llmConfig() const { return m_llmConfig; }
void IdentityProfile::setLlmConfig(const LLMConfig& config)
{
    m_llmConfig = config;
    emit changed();
}

QStringList IdentityProfile::allowedTools() const { return m_allowedTools; }
void IdentityProfile::setAllowedTools(const QStringList& tools)
{
    m_allowedTools = tools;
    emit changed();
}

int IdentityProfile::recursionDepth() const { return m_recursionDepth; }
void IdentityProfile::setRecursionDepth(int depth)
{
    if (m_recursionDepth != depth) {
        m_recursionDepth = depth;
        emit changed();
    }
}
