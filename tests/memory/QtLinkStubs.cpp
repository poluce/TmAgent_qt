#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "llm/ModelFactory.h"

// NOTE:
// This test binary only needs MemoryManager::reflectAndScore and basic persistence helpers.
// Some unrelated code paths in MemoryManager/ChatPersistenceService reference Identity/ModelFactory.
// We provide minimal link-time stubs here to keep the headless test lightweight.

// ---------------- Identity ----------------

QString Identity::id() const { return m_id; }
QString Identity::name() const { return m_name; }
void Identity::setName(const QString& name) { m_name = name; }
Identity::IdentityType Identity::type() const { return m_type; }
bool Identity::isAgent() const { return m_type == IdentityType::Agent; }
bool Identity::isUser() const { return m_type == IdentityType::User; }

IdentityProfile* Identity::profile() const { return m_profile; }
void Identity::setProfile(IdentityProfile* profile) { m_profile = profile; }

QString Identity::avatar() const { return m_avatar; }
void Identity::setAvatar(const QString& avatar) { m_avatar = avatar; }

// ---------------- IdentityProfile ----------------

QString IdentityProfile::description() const { return m_description; }
void IdentityProfile::setDescription(const QString& desc) { m_description = desc; }

QString IdentityProfile::systemPrompt() const { return m_systemPrompt; }
void IdentityProfile::setSystemPrompt(const QString& prompt) { m_systemPrompt = prompt; }

LLMConfig IdentityProfile::llmConfig() const { return m_llmConfig; }
void IdentityProfile::setLlmConfig(const LLMConfig& config) { m_llmConfig = config; }

QStringList IdentityProfile::allowedTools() const { return m_allowedTools; }
void IdentityProfile::setAllowedTools(const QStringList& tools) { m_allowedTools = tools; }

bool IdentityProfile::delegateEnabled() const { return m_delegateEnabled; }
void IdentityProfile::setDelegateEnabled(bool enabled) { m_delegateEnabled = enabled; }

int IdentityProfile::recursionDepth() const { return m_recursionDepth; }
void IdentityProfile::setRecursionDepth(int depth) { m_recursionDepth = depth; }

// ---------------- ModelFactory ----------------

ModelFactory* ModelFactory::instance() { return nullptr; }

QString ModelFactory::resolveModelId(const LLMConfig& llmConfig) const
{
    return llmConfig.selectedModelId.trimmed();
}
