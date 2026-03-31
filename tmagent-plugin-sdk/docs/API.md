# TmAgent Plugin SDK API Reference

Complete reference for all public interfaces, data structures, and helper functions in the TmAgent Plugin SDK.

## Table of Contents

- [Plugin Interfaces](#plugin-interfaces)
  - [IToolPlugin](#itoolplugin)
  - [IToolProvider](#itoolprovider)
  - [IToolPluginHost](#itoolpluginhost)
  - [IBackendPlugin](#ibackendplugin)
  - [IDelegateBackend](#idelegatebackend)
  - [ITeammateBackend](#iteammatebackend)
- [Data Structures](#data-structures)
  - [Tool](#tool)
  - [ToolCall](#toolcall)
  - [ToolResult](#toolresult)
  - [ToolPluginDescriptor](#toolplugindescriptor)
  - [BackendDescriptor](#backenddescriptor)
  - [AgentConfig](#agentconfig)
  - [TeammateConfig](#teammateconfig)
  - [TeammateState](#teammatestate)
- [Helper Functions](#helper-functions)
  - [ToolSchemaBuilder](#toolschemabuilder)
  - [Async Tool Helpers](#async-tool-helpers)

---

## Plugin Interfaces

### IToolPlugin

Base interface for tool plugins.

**Header**: `<tmagent/plugin/IToolPlugin.h>`

**Methods**:

#### descriptor()

```cpp
virtual ToolPluginDescriptor descriptor() const = 0;
```

Returns plugin metadata including ID, version, and tool list.

**Returns**: `ToolPluginDescriptor` - Plugin metadata

**Example**:
```cpp
ToolPluginDescriptor MyPlugin::descriptor() const {
    ToolPluginDescriptor desc;
    desc.pluginId = "my_plugin";
    desc.displayName = "My Plugin";
    desc.version = "1.0.0";
    desc.toolNames = QStringList{"tool1", "tool2"};
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    return desc;
}
```

#### createProvider()

```cpp
virtual IToolProvider* createProvider(IToolPluginHost* host, 
                                     QObject* parent) = 0;
```

Creates a tool provider instance.

**Parameters**:
- `host` - Host callback interface for accessing application services
- `parent` - Qt parent object for memory management

**Returns**: `IToolProvider*` - Provider instance (ownership transferred to caller)

**Example**:
```cpp
IToolProvider* MyPlugin::createProvider(IToolPluginHost* host, QObject* parent) {
    return new MyProvider(host, parent);
}
```

#### configureProvider() (Optional)

```cpp
virtual bool configureProvider(IToolProvider* provider,
                              const QJsonObject& config,
                              QString* error);
```

Configures the provider with user settings.

**Parameters**:
- `provider` - Provider instance to configure
- `config` - Configuration JSON object
- `error` - Output parameter for error message

**Returns**: `true` on success, `false` on failure

**Default Implementation**: Returns `true` (no configuration needed)

#### health() (Optional)

```cpp
virtual ToolPluginHealth health(const IToolProvider* provider) const;
```

Checks provider health status.

**Parameters**:
- `provider` - Provider instance to check

**Returns**: `ToolPluginHealth` - Health status and diagnostics

---

### IToolProvider

Interface for tool execution.

**Header**: `<tmagent/plugin/IToolProvider.h>`

**Methods**:

#### listTools()

```cpp
virtual QList<Tool> listTools() const = 0;
```

Returns list of all tools provided by this provider.

**Returns**: `QList<Tool>` - List of tool definitions

**Example**:
```cpp
QList<Tool> MyProvider::listTools() const {
    QList<Tool> tools;
    
    Tool tool;
    tool.name = "my_tool";
    tool.description = "Does something useful";
    tool.inputSchema = makeToolSchema(...);
    
    tools.append(tool);
    return tools;
}
```

#### execute()

```cpp
virtual ToolResult execute(const ToolCall& call) = 0;
```

Executes a tool call.

**Parameters**:
- `call` - Tool call request with name and parameters

**Returns**: `ToolResult` - Execution result

**Example**:
```cpp
ToolResult MyProvider::execute(const ToolCall& call) {
    if (call.name == "my_tool") {
        QString param = call.input.value("param").toString();
        // ... perform operation ...
        return ToolResult("Success", "Operation completed", true);
    }
    
    return ToolResult("Unknown tool", "Error", false,
                     QJsonObject{{"errorCode", "unknown_tool"}});
}
```

---

### IToolPluginHost

Host callback interface for plugins to access application services.

**Header**: `<tmagent/plugin/IToolPluginHost.h>`

**Methods**:

#### availableTeammateBackendIds()

```cpp
virtual QStringList availableTeammateBackendIds() const = 0;
```

Returns list of available teammate backend IDs.

#### availableTools()

```cpp
virtual QStringList availableTools() const = 0;
```

Returns list of all available tool names in the application.

#### executeHostTool()

```cpp
virtual ToolResult executeHostTool(const ToolCall& call) = 0;
```

Executes another tool in the application (tool-to-tool calls).

**Parameters**:
- `call` - Tool call to execute

**Returns**: `ToolResult` - Execution result

#### logDebug(), logInfo(), logWarning(), logError()

```cpp
virtual void logDebug(const QString& pluginId, const QString& message) = 0;
virtual void logInfo(const QString& pluginId, const QString& message) = 0;
virtual void logWarning(const QString& pluginId, const QString& message) = 0;
virtual void logError(const QString& pluginId, const QString& message) = 0;
```

Logs messages to the application log system.

**Parameters**:
- `pluginId` - Plugin identifier
- `message` - Log message

#### getPluginConfig(), setPluginConfig()

```cpp
virtual QJsonObject getPluginConfig(const QString& pluginId) const = 0;
virtual bool setPluginConfig(const QString& pluginId, 
                            const QJsonObject& config,
                            QString* error) = 0;
```

Reads and writes plugin configuration.

#### getPluginDataDir(), getAppDataDir()

```cpp
virtual QString getPluginDataDir(const QString& pluginId) const = 0;
virtual QString getAppDataDir() const = 0;
```

Returns data directory paths.

#### parseCode()

```cpp
virtual QJsonObject parseCode(const QString& language,
                              const QString& code,
                              QString* error) = 0;
```

Parses source code using tree-sitter.

**Parameters**:
- `language` - Language identifier (e.g., "cpp", "python")
- `code` - Source code to parse
- `error` - Output parameter for error message

**Returns**: `QJsonObject` - AST representation

---

### IBackendPlugin

Base interface for backend plugins.

**Header**: `<tmagent/plugin/IBackendPlugin.h>`

**Methods**:

#### descriptor()

```cpp
virtual BackendDescriptor descriptor() const = 0;
```

Returns backend metadata.

#### createDelegateBackend()

```cpp
virtual IDelegateBackend* createDelegateBackend(QObject* parent) = 0;
```

Creates a delegate backend instance.

#### createTeammateBackend()

```cpp
virtual ITeammateBackend* createTeammateBackend(QObject* parent) = 0;
```

Creates a teammate backend instance.

---

### IDelegateBackend

Interface for delegate backends (sub-task delegation).

**Header**: `<tmagent/plugin/IDelegateBackend.h>`

**Methods**:

#### backendId()

```cpp
virtual QString backendId() const = 0;
```

Returns backend identifier.

#### createSession()

```cpp
virtual std::unique_ptr<IDelegateSession> createSession(
    const DelegateRequest& request,
    const DelegateCallbacks& callbacks,
    QString* error) = 0;
```

Creates a delegation session.

**Parameters**:
- `request` - Delegation request with task and configuration
- `callbacks` - Callback functions for events
- `error` - Output parameter for error message

**Returns**: `std::unique_ptr<IDelegateSession>` - Session instance, or nullptr on failure

---

### ITeammateBackend

Interface for teammate backends (persistent collaboration).

**Header**: `<tmagent/plugin/ITeammateBackend.h>`

**Methods**:

#### ensureReady()

```cpp
virtual bool ensureReady(QString* error = nullptr) = 0;
```

Ensures backend is initialized and ready.

#### isReady()

```cpp
virtual bool isReady() const = 0;
```

Checks if backend is ready.

#### createSession()

```cpp
virtual CreateResult createSession(const QString& teammateId,
                                  const TeammateConfig& config) = 0;
```

Creates a teammate session.

**Returns**: `CreateResult` - Result with success flag and thread ID

#### sendMessage()

```cpp
virtual SendResult sendMessage(const QString& teammateId,
                              const QString& text) = 0;
```

Sends a message to a teammate.

**Returns**: `SendResult` - Result with success flag and turn ID

#### cancelTurn()

```cpp
virtual bool cancelTurn(const QString& teammateId,
                       QString* error = nullptr) = 0;
```

Cancels the current turn.

#### destroySession()

```cpp
virtual void destroySession(const QString& teammateId) = 0;
```

Destroys a teammate session.

#### shutdown()

```cpp
virtual void shutdown() = 0;
```

Shuts down the backend.

---

## Data Structures

### Tool

Defines a tool that can be called by AI agents.

**Header**: `<tmagent/types/ToolTypes.h>`

**Fields**:
- `QString name` - Tool name (unique identifier)
- `QString description` - Human-readable description
- `QJsonObject inputSchema` - JSON Schema for parameters

**Methods**:
- `QJsonObject toJson() const` - Serializes to OpenAI-compatible format

**Example**:
```cpp
Tool tool;
tool.name = "execute_shell";
tool.description = "Execute a shell command";
tool.inputSchema = makeToolSchema(
    "execute_shell",
    "Execute a shell command",
    QJsonObject{
        {"command", makePropertySchema("string", "Command to execute")}
    },
    QStringList{"command"}
)["function"].toObject()["parameters"].toObject();
```

---

### ToolCall

Represents a tool invocation request.

**Header**: `<tmagent/types/ToolTypes.h>`

**Fields**:
- `QString id` - Unique call identifier
- `QString name` - Tool name
- `QJsonObject input` - Input parameters

**Static Methods**:
- `static ToolCall fromJson(const QJsonObject& json)` - Deserializes from OpenAI format

**Example**:
```cpp
ToolCall call;
call.id = "call_123";
call.name = "execute_shell";
call.input = QJsonObject{{"command", "ls -la"}};
```

---

### ToolResult

Represents tool execution result.

**Header**: `<tmagent/types/ToolTypes.h>`

**Fields**:
- `QString rawContent` - Full result for LLM
- `QString userSummary` - Short summary for user
- `bool success` - Execution status
- `QJsonObject data` - Structured metadata (error codes, etc.)

**Constructors**:
```cpp
ToolResult();
ToolResult(const QString& raw, const QString& summary, 
          bool ok = true, const QJsonObject& extraData = {});
```

**Example**:
```cpp
// Success
ToolResult result("File contents...", "Read 100 lines", true);

// Failure
ToolResult error("Error: file not found", "File error", false,
                QJsonObject{{"errorCode", "file_not_found"}});

// Deferred (async)
ToolResult deferred("__DEFERRED__Processing...", "Started", true);
```

---

### ToolPluginDescriptor

Plugin metadata for tool plugins.

**Header**: `<tmagent/types/PluginTypes.h>`

**Fields**:
- `QString pluginId` - Unique plugin identifier
- `QString displayName` - Human-readable name
- `QString version` - Plugin version (semantic versioning)
- `QString description` - Plugin description
- `QString category` - Category (e.g., "filesystem", "network")
- `QStringList toolNames` - List of provided tool names
- `QJsonObject configSchema` - JSON Schema for configuration
- `int sdkVersionMajor` - SDK major version
- `int sdkVersionMinor` - SDK minor version

**Methods**:
- `bool isValid() const` - Validates required fields

---

### BackendDescriptor

Plugin metadata for backend plugins.

**Header**: `<tmagent/types/BackendTypes.h>`

**Fields**:
- `QString backendId` - Unique backend identifier
- `QString displayName` - Human-readable name
- `QString version` - Backend version
- `bool supportsDelegate` - Supports delegate mode
- `bool supportsTeammate` - Supports teammate mode
- `int sdkVersionMajor` - SDK major version
- `int sdkVersionMinor` - SDK minor version

**Methods**:
- `bool isValid() const` - Validates required fields

---

### AgentConfig

Configuration for AI agents.

**Header**: `<tmagent/types/CommonTypes.h>`

**Fields**:
- `QString uuid` - Agent UUID
- `QString userName` - User name
- `QString providerInstanceId` - Model provider instance ID
- `QString selectedModelId` - Selected model ID
- `QString configId` - Configuration ID (legacy)
- `QString systemPrompt` - System prompt
- `QString executionMode` - Execution mode
- `QString workspaceDir` - Workspace directory
- `int recursionDepth` - Current recursion depth

**Methods**:
- `bool isValid() const` - Validates configuration
- `bool canDelegate() const` - Checks if delegation is allowed

---

### TeammateConfig

Configuration for teammate sessions.

**Header**: `<tmagent/types/CommonTypes.h>`

**Fields**:
- `QString name` - Teammate name
- `QString role` - Teammate role
- `QString backend` - Backend ID
- `QString persistence` - "persistent" or "temporary"
- `QString workingDirectory` - Working directory
- `QString ownerAgentId` - Owner agent ID
- `int turnIdleTimeoutMs` - Idle timeout
- `bool autoCleanup` - Auto cleanup flag
- `QString ephemeralOwnerTurnId` - Ephemeral owner turn ID
- `QJsonObject backendOverrides` - Backend-specific overrides

---

### TeammateState

Current state of a teammate session.

**Header**: `<tmagent/types/CommonTypes.h>`

**Fields**:
- `QString id` - Teammate ID
- `QString threadId` - Thread ID
- `QString activeTurnId` - Active turn ID
- `QString status` - Status ("idle", "busy", "error", "shutdown")
- `QString lastError` - Last error message
- `int turnCount` - Turn count
- `qint64 createdAtMs` - Creation timestamp
- `qint64 lastActiveAtMs` - Last activity timestamp

---

## Helper Functions

### ToolSchemaBuilder

Helper functions for building JSON Schemas.

**Header**: `<tmagent/support/ToolSchemaBuilder.h>`

#### makePropertySchema()

```cpp
inline QJsonObject makePropertySchema(const QString& type, 
                                     const QString& description);
```

Creates a property schema.

**Parameters**:
- `type` - JSON type ("string", "number", "boolean", "object", "array")
- `description` - Property description

**Returns**: `QJsonObject` - Property schema

**Example**:
```cpp
QJsonObject prop = makePropertySchema("string", "File path");
// Returns: {"type": "string", "description": "File path"}
```

#### makeToolSchema()

```cpp
inline QJsonObject makeToolSchema(const QString& name,
                                 const QString& description,
                                 const QJsonObject& properties,
                                 const QStringList& required);
```

Creates a complete tool schema.

**Parameters**:
- `name` - Tool name
- `description` - Tool description
- `properties` - Properties object (name -> property schema)
- `required` - List of required property names

**Returns**: `QJsonObject` - Complete tool schema in OpenAI format

**Example**:
```cpp
QJsonObject schema = makeToolSchema(
    "read_file",
    "Read file contents",
    QJsonObject{
        {"path", makePropertySchema("string", "File path")},
        {"encoding", makePropertySchema("string", "File encoding")}
    },
    QStringList{"path"}
);
```

---

### Async Tool Helpers

Helper functions for asynchronous tool execution.

**Header**: `<tmagent/support/ToolSchemaBuilder.h>`

#### isDeferredToolResult()

```cpp
inline bool isDeferredToolResult(const QString& raw);
```

Checks if a tool result is deferred (async).

**Parameters**:
- `raw` - Raw content string

**Returns**: `true` if content starts with "__DEFERRED__"

#### stripDeferredPrefix()

```cpp
inline QString stripDeferredPrefix(const QString& raw);
```

Removes the deferred prefix from content.

**Parameters**:
- `raw` - Raw content string

**Returns**: Content without "__DEFERRED__" prefix

**Example**:
```cpp
ToolResult result = provider->execute(call);
if (isDeferredToolResult(result.rawContent)) {
    QString message = stripDeferredPrefix(result.rawContent);
    // Wait for toolCompleted signal...
}
```

---

## Error Codes

Standard error codes in `ToolResult.data["errorCode"]`:

| Code | Description | Recovery |
|------|-------------|----------|
| `unknown_tool` | Tool not found | Use different tool |
| `missing_parameter` | Required parameter missing | Provide parameter |
| `invalid_parameter` | Parameter format invalid | Fix parameter format |
| `permission_denied` | Permission denied | Request permission |
| `timeout` | Execution timeout | Retry or increase timeout |
| `execution_failed` | Execution failed | Check error details |
| `plugin_exception` | Plugin internal error | Report to plugin author |
| `service_unavailable` | External service unavailable | Retry later |

---

## Version Compatibility

### Checking Compatibility

```cpp
bool isCompatible(const ToolPluginDescriptor& desc) {
    // Major version must match
    if (desc.sdkVersionMajor != TMAGENT_SDK_VERSION_MAJOR)
        return false;
    
    // Minor version: plugin can use older SDK
    if (desc.sdkVersionMinor > TMAGENT_SDK_VERSION_MINOR)
        return false;
    
    return true;
}
```

### Version Macros

```cpp
#include <tmagent/version.h>

// Version numbers
TMAGENT_SDK_VERSION_MAJOR  // 1
TMAGENT_SDK_VERSION_MINOR  // 0
TMAGENT_SDK_VERSION_PATCH  // 0

// Version string
TMAGENT_SDK_VERSION_STRING // "1.0.0"

// Version comparison
TMAGENT_SDK_VERSION_CHECK(major, minor, patch)
TMAGENT_SDK_VERSION
```

---

## Qt Plugin System Integration

### Plugin Metadata

```cpp
Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "plugin.json")
Q_INTERFACES(TmAgent::IToolPlugin)
```

### Interface IDs

```cpp
#define TMAGENT_TOOL_PLUGIN_IID "org.tmagent.ToolPlugin/1.0"
#define TMAGENT_BACKEND_PLUGIN_IID "org.tmagent.BackendPlugin/1.0"
```

---

## See Also

- [Tutorial](TUTORIAL.md) - Step-by-step plugin development
- [Migration Guide](MIGRATION.md) - Migrating from old plugin system
- [Examples](../examples/) - Complete example plugins
