#include "MinimalToolPlugin.h"
#include <tmagent/support/ToolSchemaBuilder.h>
#include <tmagent/version.h>

// MinimalToolProvider implementation
MinimalToolProvider::MinimalToolProvider(IToolPluginHost* host, QObject* parent)
    : QObject(parent), m_host(host)
{
}

QList<Tool> MinimalToolProvider::listTools() const
{
    QList<Tool> tools;
    
    Tool echoTool;
    echoTool.name = "echo";
    echoTool.description = "Echo back the input message";
    echoTool.inputSchema = makeToolSchema(
        "echo",
        "Echo back the input message",
        QJsonObject{
            {"message", makePropertySchema("string", "The message to echo")}
        },
        QStringList{"message"}
    )["function"].toObject()["parameters"].toObject();
    
    tools.append(echoTool);
    return tools;
}

ToolResult MinimalToolProvider::execute(const ToolCall& call)
{
    if (call.name == "echo") {
        QString message = call.input.value("message").toString();
        
        if (message.isEmpty()) {
            return ToolResult(
                "Error: missing required parameter 'message'",
                "Parameter error",
                false,
                QJsonObject{{"errorCode", "missing_parameter"}}
            );
        }
        
        // Log the execution
        if (m_host) {
            m_host->logInfo("minimal_tool", QString("Echo: %1").arg(message));
        }
        
        return ToolResult(
            message,
            QString("Echoed: %1").arg(message),
            true
        );
    }
    
    return ToolResult(
        QString("Error: unknown tool '%1'").arg(call.name),
        "Unknown tool",
        false,
        QJsonObject{{"errorCode", "unknown_tool"}}
    );
}

// MinimalToolPlugin implementation
ToolPluginDescriptor MinimalToolPlugin::descriptor() const
{
    ToolPluginDescriptor desc;
    desc.pluginId = "minimal_tool";
    desc.displayName = "Minimal Tool Plugin";
    desc.version = "1.0.0";
    desc.description = "A minimal example tool plugin";
    desc.category = "example";
    desc.toolNames = QStringList{"echo"};
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    
    return desc;
}

IToolProvider* MinimalToolPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new MinimalToolProvider(host, parent);
}
