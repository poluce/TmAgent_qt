#ifndef TMAGENT_ASYNCTOOLHELPERS_H
#define TMAGENT_ASYNCTOOLHELPERS_H

#include <QString>

namespace TmAgent {

/**
 * @file AsyncToolHelpers.h
 * @brief Helper functions for asynchronous tool execution
 * 
 * These functions help detect and handle deferred tool results, which are used
 * when a tool needs to execute asynchronously and return results later.
 */

/**
 * @brief Prefix used to mark a tool result as deferred (asynchronous)
 * 
 * When a tool provider returns a ToolResult with rawContent starting with this
 * prefix, it indicates that the actual result will be provided later via the
 * toolCompleted signal.
 */
constexpr const char* DEFERRED_TOOL_PREFIX = "__DEFERRED__";

/**
 * @brief Check if a tool result is marked as deferred
 * 
 * A deferred result indicates that the tool is executing asynchronously and
 * will emit a toolCompleted signal when the actual result is ready.
 * 
 * @param rawContent The rawContent field from a ToolResult
 * @return true if the result is deferred, false otherwise
 * 
 * @example
 * @code
 * ToolResult result = provider->execute(call);
 * if (isDeferredToolResult(result.rawContent)) {
 *     // Wait for toolCompleted signal
 *     connect(provider, &IToolProvider::toolCompleted, 
 *             this, &MyClass::onToolCompleted);
 * } else {
 *     // Process result immediately
 *     processResult(result);
 * }
 * @endcode
 */
inline bool isDeferredToolResult(const QString& rawContent) {
    return rawContent.startsWith(DEFERRED_TOOL_PREFIX);
}

/**
 * @brief Remove the deferred prefix from a tool result
 * 
 * This function strips the "__DEFERRED__" prefix from the beginning of the
 * rawContent string, returning the actual message content.
 * 
 * @param rawContent The rawContent field from a ToolResult
 * @return The content without the deferred prefix, or the original string if no prefix
 * 
 * @example
 * @code
 * QString raw = "__DEFERRED__正在执行长时间任务...";
 * QString message = stripDeferredPrefix(raw);
 * // message == "正在执行长时间任务..."
 * @endcode
 */
inline QString stripDeferredPrefix(const QString& rawContent) {
    if (isDeferredToolResult(rawContent)) {
        // "__DEFERRED__" has length 13
        return rawContent.mid(13);
    }
    return rawContent;
}

/**
 * @brief Create a deferred tool result with a status message
 * 
 * Helper function to create a ToolResult that indicates asynchronous execution.
 * The provider should emit toolCompleted signal when the actual result is ready.
 * 
 * @param statusMessage Message to display while waiting (will be prefixed with __DEFERRED__)
 * @return QString with the deferred prefix
 * 
 * @example
 * @code
 * ToolResult MyProvider::execute(const ToolCall& call) {
 *     if (call.name == "long_running_task") {
 *         // Start async operation
 *         startAsyncOperation(call);
 *         
 *         // Return deferred result
 *         return ToolResult(
 *             makeDeferredResult("正在执行任务，请稍候..."),
 *             "任务已启动",
 *             true
 *         );
 *     }
 *     // ... handle other tools
 * }
 * @endcode
 */
inline QString makeDeferredResult(const QString& statusMessage) {
    return QString(DEFERRED_TOOL_PREFIX) + statusMessage;
}

} // namespace TmAgent

#endif // TMAGENT_ASYNCTOOLHELPERS_H
