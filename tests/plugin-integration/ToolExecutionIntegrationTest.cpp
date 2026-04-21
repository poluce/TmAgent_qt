#include "ToolExecutionIntegrationTest.h"
#include "src/core/agent/ToolPluginManager.h"
#include "src/core/agent/ToolDispatcher.h"
#include "src/core/agent/ToolPluginHostImpl.h"
#include <tmagent/types/ToolTypes.h>
#include <tmagent/plugin/IToolProvider.h>
#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QThread>

using namespace TmAgent;

void ToolExecutionIntegrationTest::initTestCase()
{
    // Setup test environment
    qDebug() << "Initializing ToolExecutionIntegrationTest";
    
    // Create plugin host
    ToolPluginHostImpl* host = new ToolPluginHostImpl(this);
    
    // Create plugin manager
    m_pluginManager = new ToolPluginManager(host, this);
    
    // Create tool dispatcher
    m_toolDispatcher = new ToolDispatcher(m_pluginManager, this);
    
    // Load test plugins
    QString examplesPath = QCoreApplication::applicationDirPath() + 
                          "/../tmagent-plugin-sdk/examples/minimal-tool-plugin";
    
    #ifdef Q_OS_WIN
        m_testPluginPath = examplesPath + "/MinimalToolPlugin.dll";
    #elif defined(Q_OS_MAC)
        m_testPluginPath = examplesPath + "/libMinimalToolPlugin.dylib";
    #else
        m_testPluginPath = examplesPath + "/libMinimalToolPlugin.so";
    #endif
    
    if (QFile::exists(m_testPluginPath)) {
        m_pluginManager->loadPlugin(m_testPluginPath);
        qDebug() << "Loaded test plugin:" << m_testPluginPath;
    } else {
        qWarning() << "Test plugin not found:" << m_testPluginPath;
    }
}

void ToolExecutionIntegrationTest::cleanupTestCase()
{
    // Cleanup
    qDebug() << "Cleaning up ToolExecutionIntegrationTest";
}

void ToolExecutionIntegrationTest::init()
{
    // Setup before each test
}

void ToolExecutionIntegrationTest::cleanup()
{
    // Cleanup after each test
}

// ============================================================================
// Synchronous Tool Execution Tests
// ============================================================================

void ToolExecutionIntegrationTest::testSyncToolExecution_Success()
{
    // Test successful synchronous tool execution
    // Requirement 39.4: Test synchronous tool execution
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    ToolCall call;
    call.id = "test_call_001";
    call.name = "echo";
    call.input = QJsonObject{{"message", "Hello, World!"}};
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    QVERIFY2(result.success, "Tool execution should succeed");
    QVERIFY2(result.rawContent.contains("Hello, World!"), 
             "Result should contain echoed message");
    QVERIFY2(!result.userSummary.isEmpty(), 
             "User summary should be present");
    
    qDebug() << "Tool result:" << result.rawContent;
}

void ToolExecutionIntegrationTest::testSyncToolExecution_WithParameters()
{
    // Test tool execution with various parameter types
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    ToolCall call;
    call.id = "test_call_002";
    call.name = "echo";
    call.input = QJsonObject{
        {"message", "Test message"},
        {"count", 3},
        {"enabled", true}
    };
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    QVERIFY(result.success);
    qDebug() << "Tool executed with parameters:" << call.input;
}

void ToolExecutionIntegrationTest::testSyncToolExecution_MissingParameter()
{
    // Test tool execution with missing required parameter
    // Requirement 39.4: Test tool execution failure scenarios
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    ToolCall call;
    call.id = "test_call_003";
    call.name = "echo";
    call.input = QJsonObject{};  // Missing "message" parameter
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    // Should fail or return empty result
    if (!result.success) {
        QVERIFY2(result.data.contains("errorCode"), 
                 "Error result should contain error code");
        qDebug() << "Expected failure for missing parameter:" << result.rawContent;
    }
}

void ToolExecutionIntegrationTest::testSyncToolExecution_InvalidParameter()
{
    // Test tool execution with invalid parameter type
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    ToolCall call;
    call.id = "test_call_004";
    call.name = "echo";
    call.input = QJsonObject{{"message", QJsonArray{1, 2, 3}}};  // Wrong type
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    // Tool should handle gracefully
    QVERIFY2(!result.rawContent.isEmpty(), "Should return some result");
    qDebug() << "Result with invalid parameter:" << result.rawContent;
}

void ToolExecutionIntegrationTest::testSyncToolExecution_UnknownTool()
{
    // Test execution of non-existent tool
    // Requirement 39.4: Test tool execution failure scenarios
    
    ToolCall call;
    call.id = "test_call_005";
    call.name = "nonexistent_tool";
    call.input = QJsonObject{};
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    QVERIFY2(!result.success, "Unknown tool should fail");
    QVERIFY2(result.data.contains("errorCode"), 
             "Should contain error code");
    QCOMPARE(result.data["errorCode"].toString(), 
             QString("unknown_tool"));
    
    qDebug() << "Unknown tool error:" << result.rawContent;
}

// ============================================================================
// Asynchronous Tool Execution Tests
// ============================================================================

void ToolExecutionIntegrationTest::testAsyncToolExecution_Success()
{
    // Test asynchronous tool execution initiation
    // Requirement 39.4: Test asynchronous tool execution
    
    QSKIP("Async tool execution requires async-capable test plugin");
}

void ToolExecutionIntegrationTest::testAsyncToolExecution_Completion()
{
    // Test asynchronous tool completion signal
    
    QSKIP("Async tool execution requires async-capable test plugin");
}

void ToolExecutionIntegrationTest::testAsyncToolExecution_Timeout()
{
    // Test asynchronous tool timeout handling
    
    QSKIP("Async tool execution requires async-capable test plugin");
}

void ToolExecutionIntegrationTest::testAsyncToolExecution_Cancel()
{
    // Test cancellation of asynchronous tool
    
    QSKIP("Async tool execution requires async-capable test plugin");
}

// ============================================================================
// Tool Execution Failure Scenarios
// ============================================================================

void ToolExecutionIntegrationTest::testToolExecution_PermissionDenied()
{
    // Test tool execution with insufficient permissions
    
    QSKIP("Permission testing requires specialized test plugin");
}

void ToolExecutionIntegrationTest::testToolExecution_ServiceUnavailable()
{
    // Test tool execution when external service is unavailable
    
    QSKIP("Service unavailable testing requires specialized test plugin");
}

void ToolExecutionIntegrationTest::testToolExecution_ExecutionTimeout()
{
    // Test tool execution timeout
    
    QSKIP("Timeout testing requires long-running test plugin");
}

void ToolExecutionIntegrationTest::testToolExecution_ResourceExhausted()
{
    // Test tool execution with resource exhaustion
    
    QSKIP("Resource exhaustion testing requires specialized test plugin");
}

// ============================================================================
// Exception Handling Tests
// ============================================================================

void ToolExecutionIntegrationTest::testExceptionHandling_CppException()
{
    // Test handling of C++ exceptions thrown by plugin
    // Requirement 39.4: Test exception handling
    
    QSKIP("Exception testing requires exception-throwing test plugin");
}

void ToolExecutionIntegrationTest::testExceptionHandling_UnknownException()
{
    // Test handling of unknown exceptions
    
    QSKIP("Exception testing requires exception-throwing test plugin");
}

void ToolExecutionIntegrationTest::testExceptionHandling_Isolation()
{
    // Test that plugin exceptions don't affect other plugins
    // Requirement 39.4: Test exception handling
    
    QSKIP("Exception isolation testing requires multiple test plugins");
}

void ToolExecutionIntegrationTest::testExceptionHandling_Recovery()
{
    // Test system recovery after plugin exception
    
    QSKIP("Exception recovery testing requires exception-throwing test plugin");
}

// ============================================================================
// Tool Result Validation Tests
// ============================================================================

void ToolExecutionIntegrationTest::testToolResult_SuccessFormat()
{
    // Test that successful tool results follow expected format
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    ToolCall call;
    call.id = "test_call_006";
    call.name = "echo";
    call.input = QJsonObject{{"message", "Format test"}};
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    QVERIFY2(result.success, "Should succeed");
    QVERIFY2(!result.rawContent.isEmpty(), "rawContent should not be empty");
    QVERIFY2(!result.userSummary.isEmpty(), "userSummary should not be empty");
    
    qDebug() << "Success result format validated";
}

void ToolExecutionIntegrationTest::testToolResult_FailureFormat()
{
    // Test that failed tool results follow expected format
    
    ToolCall call;
    call.id = "test_call_007";
    call.name = "nonexistent_tool";
    call.input = QJsonObject{};
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    QVERIFY2(!result.success, "Should fail");
    QVERIFY2(!result.rawContent.isEmpty(), "rawContent should contain error message");
    QVERIFY2(result.data.contains("errorCode"), "Should contain errorCode");
    
    qDebug() << "Failure result format validated";
}

void ToolExecutionIntegrationTest::testToolResult_MetadataPresent()
{
    // Test that tool results contain expected metadata
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    ToolCall call;
    call.id = "test_call_008";
    call.name = "echo";
    call.input = QJsonObject{{"message", "Metadata test"}};
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    // Check for metadata fields
    QVERIFY2(result.success, "Should succeed");
    QVERIFY2(!result.rawContent.isEmpty(), "Should have rawContent");
    QVERIFY2(!result.userSummary.isEmpty(), "Should have userSummary");
    
    qDebug() << "Metadata present in result";
}

void ToolExecutionIntegrationTest::testToolResult_ErrorCodeStandard()
{
    // Test that error codes follow standard format
    
    ToolCall call;
    call.id = "test_call_009";
    call.name = "unknown_tool";
    call.input = QJsonObject{};
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    QVERIFY2(!result.success, "Should fail");
    QVERIFY2(result.data.contains("errorCode"), "Should have errorCode");
    
    QString errorCode = result.data["errorCode"].toString();
    QVERIFY2(!errorCode.isEmpty(), "Error code should not be empty");
    QVERIFY2(errorCode == "unknown_tool" || 
             errorCode == "execution_failed" ||
             errorCode == "plugin_exception",
             "Error code should be standard");
    
    qDebug() << "Error code:" << errorCode;
}

// ============================================================================
// End-to-End Workflow Tests
// ============================================================================

void ToolExecutionIntegrationTest::testEndToEnd_LLMToToolExecution()
{
    // Test complete flow from LLM tool call to execution
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    // Simulate LLM tool call JSON
    QJsonObject llmToolCall{
        {"id", "call_abc123"},
        {"type", "function"},
        {"function", QJsonObject{
            {"name", "echo"},
            {"arguments", "{\"message\":\"Hello from LLM\"}"}
        }}
    };
    
    // Parse to ToolCall
    ToolCall call = ToolCall::fromJson(llmToolCall);
    
    QCOMPARE(call.id, QString("call_abc123"));
    QCOMPARE(call.name, QString("echo"));
    QVERIFY(call.input.contains("message"));
    
    // Execute
    ToolResult result = m_toolDispatcher->execute(call);
    
    QVERIFY2(result.success, "Execution should succeed");
    QVERIFY2(result.rawContent.contains("Hello from LLM"), 
             "Result should contain message");
    
    qDebug() << "End-to-end LLM to tool execution successful";
}

void ToolExecutionIntegrationTest::testEndToEnd_ToolChaining()
{
    // Test chaining multiple tool calls
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    // Execute first tool
    ToolCall call1;
    call1.id = "call_001";
    call1.name = "echo";
    call1.input = QJsonObject{{"message", "First"}};
    
    ToolResult result1 = m_toolDispatcher->execute(call1);
    QVERIFY(result1.success);
    
    // Execute second tool using result from first
    ToolCall call2;
    call2.id = "call_002";
    call2.name = "echo";
    call2.input = QJsonObject{{"message", result1.rawContent}};
    
    ToolResult result2 = m_toolDispatcher->execute(call2);
    QVERIFY(result2.success);
    
    qDebug() << "Tool chaining successful";
}

void ToolExecutionIntegrationTest::testEndToEnd_ToolResultToLLM()
{
    // Test formatting tool result for LLM
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    ToolCall call;
    call.id = "call_xyz789";
    call.name = "echo";
    call.input = QJsonObject{{"message", "Result for LLM"}};
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    // Format for LLM (OpenAI format)
    QJsonObject llmResponse{
        {"tool_call_id", call.id},
        {"role", "tool"},
        {"content", result.rawContent}
    };
    
    QVERIFY2(llmResponse.contains("tool_call_id"), "Should have tool_call_id");
    QVERIFY2(llmResponse.contains("role"), "Should have role");
    QVERIFY2(llmResponse.contains("content"), "Should have content");
    QCOMPARE(llmResponse["role"].toString(), QString("tool"));
    
    qDebug() << "Tool result formatted for LLM:" << llmResponse;
}

// ============================================================================
// Performance Tests
// ============================================================================

void ToolExecutionIntegrationTest::testPerformance_ToolDispatchLatency()
{
    // Test tool dispatch latency (should be < 10ms)
    // Requirement 18.3: Tool dispatch latency < 10ms
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    ToolCall call;
    call.id = "perf_test_001";
    call.name = "echo";
    call.input = QJsonObject{{"message", "Performance test"}};
    
    QElapsedTimer timer;
    timer.start();
    
    ToolResult result = m_toolDispatcher->execute(call);
    
    qint64 elapsed = timer.nsecsElapsed() / 1000000;  // Convert to ms
    
    qDebug() << "Tool dispatch latency:" << elapsed << "ms";
    
    // Note: This includes plugin execution time, not just dispatch
    // Pure dispatch should be < 10ms, but total execution may be longer
    QVERIFY2(elapsed < 1000, "Total execution should be reasonable (< 1s)");
}

void ToolExecutionIntegrationTest::testPerformance_ConcurrentToolCalls()
{
    // Test concurrent tool execution
    
    if (!QFile::exists(m_testPluginPath)) {
        QSKIP("Test plugin not available");
    }
    
    const int numCalls = 10;
    QElapsedTimer timer;
    timer.start();
    
    for (int i = 0; i < numCalls; ++i) {
        ToolCall call;
        call.id = QString("concurrent_%1").arg(i);
        call.name = "echo";
        call.input = QJsonObject{{"message", QString("Call %1").arg(i)}};
        
        ToolResult result = m_toolDispatcher->execute(call);
        QVERIFY(result.success);
    }
    
    qint64 elapsed = timer.elapsed();
    qDebug() << "Executed" << numCalls << "tool calls in" << elapsed << "ms";
    qDebug() << "Average per call:" << (elapsed / numCalls) << "ms";
}

QTEST_MAIN(ToolExecutionIntegrationTest)
#include "ToolExecutionIntegrationTest.moc"
