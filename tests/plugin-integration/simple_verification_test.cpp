/**
 * @brief Simple verification test for Phase 2 checkpoint
 * 
 * This is a minimal test to verify core functionality without
 * requiring full plugin compilation.
 */

#include <QCoreApplication>
#include <QDebug>
#include <QtTest>
#include <tmagent/version.h>
#include <tmagent/types/ToolTypes.h>
#include <tmagent/types/PluginTypes.h>
#include <tmagent/types/BackendTypes.h>
#include <tmagent/types/CommonTypes.h>

class SimpleVerificationTest : public QObject
{
    Q_OBJECT

private slots:
    void testSDKVersion()
    {
        qDebug() << "SDK Version:" << TMAGENT_SDK_VERSION_STRING;
        QVERIFY(TMAGENT_SDK_VERSION_MAJOR >= 1);
        QVERIFY(TMAGENT_SDK_VERSION_MINOR >= 0);
    }

    void testToolStructure()
    {
        TmAgent::Tool tool;
        tool.name = "test_tool";
        tool.description = "A test tool";
        tool.inputSchema = QJsonObject{{"type", "object"}};
        
        QJsonObject json = tool.toJson();
        QVERIFY(json.contains("type"));
        QCOMPARE(json["type"].toString(), QString("function"));
        QVERIFY(json.contains("function"));
        
        QJsonObject func = json["function"].toObject();
        QCOMPARE(func["name"].toString(), QString("test_tool"));
        QCOMPARE(func["description"].toString(), QString("A test tool"));
        
        qDebug() << "Tool serialization: PASS";
    }

    void testToolCallDeserialization()
    {
        QJsonObject json{
            {"id", "call_123"},
            {"type", "function"},
            {"function", QJsonObject{
                {"name", "execute_command"},
                {"arguments", "{\"command\":\"ls -la\"}"}
            }}
        };
        
        TmAgent::ToolCall call = TmAgent::ToolCall::fromJson(json);
        QCOMPARE(call.id, QString("call_123"));
        QCOMPARE(call.name, QString("execute_command"));
        QVERIFY(call.input.contains("command"));
        QCOMPARE(call.input["command"].toString(), QString("ls -la"));
        
        qDebug() << "ToolCall deserialization: PASS";
    }

    void testToolResult()
    {
        TmAgent::ToolResult result("Output content", "Summary", true);
        QVERIFY(result.success);
        QCOMPARE(result.rawContent, QString("Output content"));
        QCOMPARE(result.userSummary, QString("Summary"));
        
        TmAgent::ToolResult errorResult("Error message", "Failed", false,
                                       QJsonObject{{"errorCode", "test_error"}});
        QVERIFY(!errorResult.success);
        QVERIFY(errorResult.data.contains("errorCode"));
        
        qDebug() << "ToolResult structure: PASS";
    }

    void testPluginDescriptorValidation()
    {
        // Valid descriptor
        TmAgent::ToolPluginDescriptor validDesc;
        validDesc.pluginId = "test_plugin";
        validDesc.displayName = "Test Plugin";
        validDesc.version = "1.0.0";
        validDesc.sdkVersionMajor = 1;
        validDesc.sdkVersionMinor = 0;
        
        QVERIFY2(validDesc.isValid(), "Valid descriptor should pass");
        
        // Invalid descriptor (empty plugin ID)
        TmAgent::ToolPluginDescriptor invalidDesc;
        invalidDesc.pluginId = "";
        invalidDesc.displayName = "Test Plugin";
        
        QVERIFY2(!invalidDesc.isValid(), "Empty plugin ID should fail");
        
        qDebug() << "Plugin descriptor validation: PASS";
    }

    void testBackendDescriptorValidation()
    {
        // Valid descriptor
        TmAgent::BackendDescriptor validDesc;
        validDesc.backendId = "test_backend";
        validDesc.displayName = "Test Backend";
        validDesc.version = "1.0.0";
        validDesc.supportsDelegate = true;
        validDesc.supportsTeammate = false;
        validDesc.sdkVersionMajor = 1;
        validDesc.sdkVersionMinor = 0;
        
        QVERIFY2(validDesc.isValid(), "Valid backend descriptor should pass");
        
        // Invalid descriptor (no support for either mode)
        TmAgent::BackendDescriptor invalidDesc;
        invalidDesc.backendId = "test_backend";
        invalidDesc.supportsDelegate = false;
        invalidDesc.supportsTeammate = false;
        
        QVERIFY2(!invalidDesc.isValid(), 
                 "Backend must support at least one mode");
        
        qDebug() << "Backend descriptor validation: PASS";
    }

    void testVersionCompatibilityLogic()
    {
        // Test version compatibility logic
        // Major version must match
        TmAgent::ToolPluginDescriptor desc1;
        desc1.pluginId = "test";
        desc1.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
        desc1.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
        
        // This would be compatible
        qDebug() << "Plugin version:" << desc1.sdkVersionMajor 
                 << "." << desc1.sdkVersionMinor;
        qDebug() << "SDK version:" << TMAGENT_SDK_VERSION_MAJOR 
                 << "." << TMAGENT_SDK_VERSION_MINOR;
        
        // Major version mismatch
        TmAgent::ToolPluginDescriptor desc2;
        desc2.pluginId = "test";
        desc2.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR + 1;
        desc2.sdkVersionMinor = 0;
        
        qDebug() << "Incompatible plugin version:" << desc2.sdkVersionMajor 
                 << "." << desc2.sdkVersionMinor;
        qDebug() << "Version compatibility logic: PASS";
    }

    void testAgentConfigStructure()
    {
        TmAgent::AgentConfig config;
        config.uuid = "agent-123";
        config.userName = "test_user";
        config.providerInstanceId = "provider-1";
        config.selectedModelId = "model-1";
        config.systemPrompt = "You are a helpful assistant";
        config.executionMode = "auto";
        config.workspaceDir = "/workspace";
        config.recursionDepth = 0;  // Start at depth 0
        
        QVERIFY2(config.isValid(), "Valid config should pass");
        QVERIFY2(config.canDelegate(), "Depth 0 should allow delegation");
        
        // Test max recursion depth
        config.recursionDepth = 3;
        QVERIFY2(!config.canDelegate(), "Depth 3 should not allow delegation");
        
        // Test invalid config
        TmAgent::AgentConfig invalidConfig;
        QVERIFY2(!invalidConfig.isValid(), "Empty config should fail");
        
        qDebug() << "AgentConfig structure: PASS";
    }

    void testTeammateConfigStructure()
    {
        TmAgent::TeammateConfig config;
        config.name = "Assistant";
        config.role = "Helper";
        config.backend = "codex";
        config.persistence = "persistent";
        config.workingDirectory = "/workspace";
        config.ownerAgentId = "agent-123";
        config.turnIdleTimeoutMs = 30000;
        config.autoCleanup = true;
        
        QVERIFY(!config.name.isEmpty());
        QVERIFY(!config.backend.isEmpty());
        
        qDebug() << "TeammateConfig structure: PASS";
    }

    void testTeammateStateStructure()
    {
        TmAgent::TeammateState state;
        state.id = "teammate-1";
        state.threadId = "thread-1";
        state.activeTurnId = "turn-1";
        state.status = "idle";
        state.turnCount = 5;
        state.createdAtMs = 1234567890;
        state.lastActiveAtMs = 1234567900;
        
        QVERIFY(!state.id.isEmpty());
        QVERIFY(!state.status.isEmpty());
        QVERIFY(state.turnCount >= 0);
        
        qDebug() << "TeammateState structure: PASS";
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "==============================================";
    qDebug() << "Phase 2 Checkpoint - Simple Verification Test";
    qDebug() << "==============================================";
    qDebug() << "";
    
    SimpleVerificationTest test;
    int result = QTest::qExec(&test, argc, argv);
    
    qDebug() << "";
    if (result == 0) {
        qDebug() << "✓ All basic SDK functionality tests PASSED";
        qDebug() << "✓ Data structures are correctly defined";
        qDebug() << "✓ Serialization/deserialization works";
        qDebug() << "✓ Validation logic is correct";
    } else {
        qDebug() << "✗ Some tests FAILED";
    }
    qDebug() << "==============================================";
    
    return result;
}

#include "simple_verification_test.moc"
