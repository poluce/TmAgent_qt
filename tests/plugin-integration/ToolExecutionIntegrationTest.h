#ifndef TOOLEXECUTIONINTEGRATIONTEST_H
#define TOOLEXECUTIONINTEGRATIONTEST_H

#include <QObject>
#include <QtTest>

/**
 * @brief Integration tests for end-to-end tool execution flow
 * 
 * Tests requirement 39.4:
 * - Synchronous tool execution
 * - Asynchronous tool execution
 * - Tool execution failure scenarios
 * - Exception handling across plugin boundaries
 */
class ToolExecutionIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    // Test setup and cleanup
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Synchronous tool execution tests
    void testSyncToolExecution_Success();
    void testSyncToolExecution_WithParameters();
    void testSyncToolExecution_MissingParameter();
    void testSyncToolExecution_InvalidParameter();
    void testSyncToolExecution_UnknownTool();
    
    // Asynchronous tool execution tests
    void testAsyncToolExecution_Success();
    void testAsyncToolExecution_Completion();
    void testAsyncToolExecution_Timeout();
    void testAsyncToolExecution_Cancel();
    
    // Tool execution failure scenarios
    void testToolExecution_PermissionDenied();
    void testToolExecution_ServiceUnavailable();
    void testToolExecution_ExecutionTimeout();
    void testToolExecution_ResourceExhausted();
    
    // Exception handling tests
    void testExceptionHandling_CppException();
    void testExceptionHandling_UnknownException();
    void testExceptionHandling_Isolation();
    void testExceptionHandling_Recovery();
    
    // Tool result validation tests
    void testToolResult_SuccessFormat();
    void testToolResult_FailureFormat();
    void testToolResult_MetadataPresent();
    void testToolResult_ErrorCodeStandard();
    
    // End-to-end workflow tests
    void testEndToEnd_LLMToToolExecution();
    void testEndToEnd_ToolChaining();
    void testEndToEnd_ToolResultToLLM();
    
    // Performance tests
    void testPerformance_ToolDispatchLatency();
    void testPerformance_ConcurrentToolCalls();

private:
    QString m_testPluginPath;
    class PluginManager* m_pluginManager;
    class ToolDispatcher* m_toolDispatcher;
};

#endif // TOOLEXECUTIONINTEGRATIONTEST_H
