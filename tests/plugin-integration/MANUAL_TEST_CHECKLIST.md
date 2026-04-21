# TmAgent Plugin SDK - Manual Test Checklist
## Task 29.2: 手动测试关键功能

**Date**: _____________  
**Tester**: _____________  
**Version**: SDK 1.0.0  

---

## Test Environment

- [ ] Operating System: _______________
- [ ] Qt Version: _______________
- [ ] Compiler: _______________
- [ ] Build Configuration: Release / Debug

---

## 1. Plugin Loading Tests

### 1.1 Tool Plugins

Test that all tool plugins load successfully:

- [ ] **WorkspaceToolsPlugin**
  - [ ] Plugin loads without errors
  - [ ] Plugin descriptor is valid
  - [ ] All tools are registered

- [ ] **ShellToolsPlugin**
  - [ ] Plugin loads without errors
  - [ ] Plugin descriptor is valid
  - [ ] All tools are registered

- [ ] **WebToolsPlugin**
  - [ ] Plugin loads without errors
  - [ ] Plugin descriptor is valid
  - [ ] All tools are registered

- [ ] **MemoryToolsPlugin**
  - [ ] Plugin loads without errors
  - [ ] Plugin descriptor is valid
  - [ ] All tools are registered

- [ ] **SchedulerToolsPlugin**
  - [ ] Plugin loads without errors
  - [ ] Plugin descriptor is valid
  - [ ] All tools are registered

- [ ] **CoordinationToolsPlugin** (if migrated)
  - [ ] Plugin loads without errors
  - [ ] Plugin descriptor is valid
  - [ ] All tools are registered

- [ ] **CodeIntelToolsPlugin** (if migrated)
  - [ ] Plugin loads without errors
  - [ ] Plugin descriptor is valid
  - [ ] All tools are registered

### 1.2 Backend Plugins

- [ ] **CodexBackendPlugin**
  - [ ] Plugin loads without errors
  - [ ] Backend descriptor is valid
  - [ ] Supports delegate mode
  - [ ] Supports teammate mode

- [ ] **TmAgentBackendPlugin** (if migrated)
  - [ ] Plugin loads without errors
  - [ ] Backend descriptor is valid
  - [ ] Supports delegate mode
  - [ ] Supports teammate mode

---

## 2. Tool Functionality Tests

### 2.1 Workspace Tools

Test each workspace tool:

- [ ] **list_files**
  - [ ] Lists files in current directory
  - [ ] Handles recursive listing
  - [ ] Returns correct file count
  - [ ] Error handling for invalid paths

- [ ] **read_file**
  - [ ] Reads text files correctly
  - [ ] Handles UTF-8 encoding
  - [ ] Error handling for missing files
  - [ ] Error handling for binary files

- [ ] **write_file**
  - [ ] Creates new files
  - [ ] Overwrites existing files
  - [ ] Creates directories if needed
  - [ ] Error handling for permission denied

- [ ] **delete_file**
  - [ ] Deletes files successfully
  - [ ] Error handling for missing files
  - [ ] Error handling for permission denied

- [ ] **create_directory**
  - [ ] Creates directories
  - [ ] Creates nested directories
  - [ ] Error handling for existing directories

- [ ] **search_files**
  - [ ] Searches by pattern
  - [ ] Returns correct results
  - [ ] Handles regex patterns

### 2.2 Shell Tools

- [ ] **execute_shell**
  - [ ] Executes simple commands (e.g., `echo hello`)
  - [ ] Captures stdout correctly
  - [ ] Captures stderr correctly
  - [ ] Returns exit code
  - [ ] Handles command timeout
  - [ ] Error handling for invalid commands

- [ ] **get_cwd**
  - [ ] Returns current working directory
  - [ ] Path format is correct

- [ ] **set_cwd**
  - [ ] Changes working directory
  - [ ] Affects subsequent commands
  - [ ] Error handling for invalid paths

### 2.3 Web Tools

- [ ] **web_search**
  - [ ] Performs web search
  - [ ] Returns search results
  - [ ] Handles search errors
  - [ ] Respects rate limits

- [ ] **web_fetch**
  - [ ] Fetches web pages
  - [ ] Returns page content
  - [ ] Handles HTTP errors
  - [ ] Handles timeouts

### 2.4 Memory Tools

- [ ] **store_memory**
  - [ ] Stores memories successfully
  - [ ] Assigns unique IDs
  - [ ] Handles metadata
  - [ ] Error handling for invalid input

- [ ] **search_memory**
  - [ ] Searches memories by query
  - [ ] Returns relevant results
  - [ ] Ranks results correctly
  - [ ] Handles empty results

- [ ] **list_memories**
  - [ ] Lists all memories
  - [ ] Supports pagination
  - [ ] Returns correct count

- [ ] **delete_memory**
  - [ ] Deletes memories by ID
  - [ ] Error handling for invalid IDs

### 2.5 Scheduler Tools

- [ ] **schedule_task**
  - [ ] Schedules tasks successfully
  - [ ] Handles cron expressions
  - [ ] Returns task ID

- [ ] **list_scheduled_tasks**
  - [ ] Lists all scheduled tasks
  - [ ] Shows task status

- [ ] **cancel_scheduled_task**
  - [ ] Cancels tasks by ID
  - [ ] Error handling for invalid IDs

---

## 3. Tool Inter-Calling Tests

Test that tools can call other tools through IToolPluginHost:

- [ ] **Composite Operation Test**
  - [ ] Tool A calls Tool B successfully
  - [ ] Results are passed correctly
  - [ ] Error handling works across tools

- [ ] **Chain Calling Test**
  - [ ] Tool A → Tool B → Tool C chain works
  - [ ] Results propagate correctly
  - [ ] Errors propagate correctly

**Example Test Case**:
```
1. Use web_fetch to download a file
2. Use write_file to save the content
3. Use read_file to verify the content
4. Use delete_file to clean up
```

- [ ] Test case executed successfully
- [ ] All steps completed without errors
- [ ] Results are correct

---

## 4. Backend Functionality Tests

### 4.1 Delegate Backend

- [ ] **Create Delegate Session**
  - [ ] Session creates successfully
  - [ ] Returns session ID
  - [ ] Accepts DelegateRequest correctly

- [ ] **Execute Delegate Task**
  - [ ] Task executes successfully
  - [ ] Callbacks are invoked
  - [ ] Results are returned

- [ ] **Delegate Tool Execution**
  - [ ] Delegate can call tools
  - [ ] Tool results are correct
  - [ ] IToolExecutor works correctly

- [ ] **Cancel Delegate Session**
  - [ ] Session cancels successfully
  - [ ] onFailure callback is invoked
  - [ ] Resources are cleaned up

### 4.2 Teammate Backend

- [ ] **Create Teammate Session**
  - [ ] Session creates successfully
  - [ ] Returns thread ID
  - [ ] TeammateConfig is applied

- [ ] **Send Message to Teammate**
  - [ ] Message sends successfully
  - [ ] Returns turn ID
  - [ ] Response is received

- [ ] **Cancel Teammate Turn**
  - [ ] Turn cancels successfully
  - [ ] Teammate stops processing

- [ ] **Destroy Teammate Session**
  - [ ] Session destroys successfully
  - [ ] Resources are cleaned up

---

## 5. Error Handling Tests

### 5.1 Plugin Errors

- [ ] **Invalid Plugin**
  - [ ] Invalid plugin is rejected
  - [ ] Error is logged
  - [ ] Other plugins continue to work

- [ ] **Plugin Exception**
  - [ ] Exception is caught at boundary
  - [ ] Converted to ToolResult{success=false}
  - [ ] Other plugins continue to work

### 5.2 Tool Errors

- [ ] **Missing Parameter**
  - [ ] Returns error code "missing_parameter"
  - [ ] Error message is clear

- [ ] **Invalid Parameter**
  - [ ] Returns error code "invalid_parameter"
  - [ ] Error message is clear

- [ ] **Tool Timeout**
  - [ ] Returns error code "timeout"
  - [ ] Tool is terminated

- [ ] **Permission Denied**
  - [ ] Returns error code "permission_denied"
  - [ ] Error message is clear

### 5.3 Version Compatibility Errors

- [ ] **Incompatible Major Version**
  - [ ] Plugin is rejected
  - [ ] Error is logged with version info

- [ ] **Incompatible Minor Version**
  - [ ] Plugin is rejected
  - [ ] Error is logged with version info

---

## 6. Asynchronous Tool Tests

- [ ] **Deferred Tool Result**
  - [ ] Tool returns "__DEFERRED__" prefix
  - [ ] Agent receives deferred result
  - [ ] toolCompleted signal is emitted
  - [ ] Final result is received

- [ ] **Long-Running Task**
  - [ ] Task runs in background
  - [ ] UI remains responsive
  - [ ] Result is delivered when complete

---

## 7. Configuration and Logging Tests

### 7.1 Plugin Configuration

- [ ] **Get Plugin Config**
  - [ ] Returns correct configuration
  - [ ] Handles missing config

- [ ] **Set Plugin Config**
  - [ ] Saves configuration successfully
  - [ ] Configuration persists across restarts

- [ ] **Plugin Data Directory**
  - [ ] Returns correct directory path
  - [ ] Directory is created if missing
  - [ ] Plugin can read/write files

### 7.2 Logging

- [ ] **Log Debug**
  - [ ] Debug messages are logged
  - [ ] Plugin ID is included

- [ ] **Log Info**
  - [ ] Info messages are logged
  - [ ] Plugin ID is included

- [ ] **Log Warning**
  - [ ] Warning messages are logged
  - [ ] Plugin ID is included

- [ ] **Log Error**
  - [ ] Error messages are logged
  - [ ] Plugin ID is included

---

## 8. Performance Tests (Manual Observation)

### 8.1 Plugin Loading

- [ ] **Single Plugin Load**
  - [ ] Loads in < 50ms (observed)
  - [ ] No noticeable delay

- [ ] **Multiple Plugins Load**
  - [ ] All plugins load in < 200ms (observed)
  - [ ] Application starts quickly

### 8.2 Tool Execution

- [ ] **Tool Call Dispatch**
  - [ ] Dispatch is fast (< 10ms observed)
  - [ ] No noticeable delay

- [ ] **Tool Execution**
  - [ ] Simple tools execute quickly
  - [ ] Complex tools show progress

### 8.3 Memory Usage

- [ ] **Plugin Memory**
  - [ ] Each plugin uses < 5MB (observed in Task Manager)
  - [ ] No memory leaks observed

---

## 9. Regression Tests (Compare with Pre-Migration)

### 9.1 Functionality Comparison

- [ ] **All tools work as before**
  - [ ] No functionality lost
  - [ ] No behavior changes

- [ ] **All backends work as before**
  - [ ] Delegate works as before
  - [ ] Teammate works as before

### 9.2 Performance Comparison

- [ ] **Performance is similar or better**
  - [ ] No significant slowdown
  - [ ] Startup time is acceptable

---

## 10. Edge Cases and Stress Tests

### 10.1 Edge Cases

- [ ] **Empty Input**
  - [ ] Tools handle empty input gracefully

- [ ] **Large Input**
  - [ ] Tools handle large input (> 1MB)
  - [ ] No crashes or hangs

- [ ] **Special Characters**
  - [ ] Tools handle special characters
  - [ ] Unicode support works

### 10.2 Stress Tests

- [ ] **Rapid Tool Calls**
  - [ ] 100 rapid tool calls succeed
  - [ ] No crashes or errors

- [ ] **Concurrent Tool Calls**
  - [ ] Multiple tools execute concurrently
  - [ ] Results are correct
  - [ ] No race conditions

- [ ] **Long-Running Session**
  - [ ] Application runs for 1+ hour
  - [ ] No memory leaks
  - [ ] No performance degradation

---

## Test Summary

### Statistics

- Total Test Items: _______
- Passed: _______
- Failed: _______
- Skipped: _______
- Pass Rate: _______%

### Issues Found

| # | Issue Description | Severity | Status |
|---|-------------------|----------|--------|
| 1 |                   |          |        |
| 2 |                   |          |        |
| 3 |                   |          |        |

### Overall Assessment

- [ ] ✓ All critical tests passed
- [ ] ✓ No major issues found
- [ ] ✓ Ready for production

**Comments**:
```
_________________________________________________________________
_________________________________________________________________
_________________________________________________________________
```

### Recommendations

```
_________________________________________________________________
_________________________________________________________________
_________________________________________________________________
```

---

**Tester Signature**: _____________  
**Date**: _____________  
**Status**: PASS / FAIL / CONDITIONAL PASS
