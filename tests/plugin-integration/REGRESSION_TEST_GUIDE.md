# TmAgent Plugin SDK - Regression Test Execution Guide
## Task 29: 回归测试

This guide provides step-by-step instructions for executing the complete regression test suite for the TmAgent Plugin SDK architecture.

---

## Prerequisites

### Required Software
- Qt 5.15+ or Qt 6.2+
- MinGW GCC 7.3+ (Windows) or GCC 9+ (Linux) or Clang 12+ (macOS)
- PowerShell 5.1+ (for automated scripts)
- Git (for version control)

### Required Build Artifacts
- TmAgent application (built)
- All migrated plugins (built)
- SDK headers (installed)
- Test executables (built)

---

## Test Execution Overview

The regression test suite is divided into three main tasks:

1. **Task 29.1**: Run complete test suite (unit + integration tests)
2. **Task 29.2**: Manual testing of key functionality
3. **Task 29.3**: Performance benchmark tests

---

## Task 29.1: Run Complete Test Suite

### Step 1: Build All Tests

```powershell
# Navigate to test directory
cd tests/plugin-integration

# Build simple verification test
mkdir build-simple
cd build-simple
qmake ../simple_verification_test.pro
mingw32-make -j4
cd ..

# Build plugin loading test
mkdir build-loading
cd build-loading
qmake ../PluginLoadingIntegrationTest.pro
mingw32-make -j4
cd ..

# Build version compatibility test
mkdir build-version
cd build-version
qmake ../VersionCompatibilityTest.pro
mingw32-make -j4
cd ..

# Build tool execution test
mkdir build-execution
cd build-execution
qmake ../ToolExecutionIntegrationTest.pro
mingw32-make -j4
cd ..

# Build performance benchmark
mkdir build-perf
cd build-perf
qmake ../performance_benchmark.pro
mingw32-make -j4
cd ..
```

### Step 2: Run Automated Test Script

```powershell
# Run the complete regression test suite
.\run_regression_tests.ps1

# Or run with options:
.\run_regression_tests.ps1 -Verbose              # Show detailed output
.\run_regression_tests.ps1 -SkipUnit             # Skip unit tests
.\run_regression_tests.ps1 -SkipIntegration      # Skip integration tests
.\run_regression_tests.ps1 -SkipPerformance      # Skip performance tests
```

### Step 3: Review Test Results

The script will generate a report file: `REGRESSION_TEST_REPORT_<timestamp>.md`

Review the report for:
- Pass/fail status of each test
- Performance metrics
- Any issues or failures

### Expected Results

**Unit Tests**:
- ✓ SDK Data Structures Test: All tests pass
- ✓ Plugin Loading Integration Test: All tests pass
- ✓ Version Compatibility Test: All tests pass
- ✓ Tool Execution Integration Test: All tests pass

**Integration Tests**:
- ✓ TreeSitter Parser Test: 14/14 pass
- ✓ Message Router Test: All tests pass
- ✓ Task State Service Test: All tests pass
- ✓ Scheduler Service Test: All tests pass
- ✓ Memory Reflection Test: All tests pass
- ✓ Memory Tool Test: All tests pass

---

## Task 29.2: Manual Testing of Key Functionality

### Step 1: Prepare Test Environment

1. Build TmAgent application in Release mode
2. Ensure all plugins are built and in the correct directories
3. Open the manual test checklist: `MANUAL_TEST_CHECKLIST.md`

### Step 2: Launch TmAgent

```powershell
# Navigate to build directory
cd ../../build

# Launch TmAgent
.\TmAgent.exe
```

### Step 3: Execute Manual Tests

Follow the checklist in `MANUAL_TEST_CHECKLIST.md`:

1. **Plugin Loading Tests** (Section 1)
   - Verify all tool plugins load
   - Verify all backend plugins load
   - Check for any error messages in logs

2. **Tool Functionality Tests** (Section 2)
   - Test each tool category:
     - Workspace tools (list_files, read_file, write_file, etc.)
     - Shell tools (execute_shell, get_cwd, set_cwd)
     - Web tools (web_search, web_fetch)
     - Memory tools (store_memory, search_memory)
     - Scheduler tools (schedule_task, list_scheduled_tasks)

3. **Tool Inter-Calling Tests** (Section 3)
   - Test composite operations
   - Test chain calling
   - Verify error propagation

4. **Backend Functionality Tests** (Section 4)
   - Test delegate backend
   - Test teammate backend
   - Verify callbacks work correctly

5. **Error Handling Tests** (Section 5)
   - Test invalid plugin handling
   - Test plugin exceptions
   - Test tool errors
   - Test version compatibility errors

6. **Asynchronous Tool Tests** (Section 6)
   - Test deferred tool results
   - Test long-running tasks

7. **Configuration and Logging Tests** (Section 7)
   - Test plugin configuration
   - Test logging functionality

8. **Performance Tests** (Section 8)
   - Observe plugin loading speed
   - Observe tool execution speed
   - Monitor memory usage

9. **Regression Tests** (Section 9)
   - Compare functionality with pre-migration
   - Verify no functionality lost

10. **Edge Cases and Stress Tests** (Section 10)
    - Test edge cases
    - Run stress tests

### Step 4: Document Results

Fill out the checklist as you complete each test:
- Mark items as passed/failed
- Document any issues found
- Add comments and recommendations

---

## Task 29.3: Performance Benchmark Tests

### Step 1: Build Performance Benchmark

```powershell
cd tests/plugin-integration/build-perf
qmake ../performance_benchmark.pro
mingw32-make -j4
```

### Step 2: Run Performance Benchmark

```powershell
.\release\performance_benchmark.exe
```

### Step 3: Review Performance Results

The benchmark will test:

1. **Single Plugin Load Time** (需求 18.1)
   - Target: < 50ms per plugin
   - Measures: Average load time for each plugin

2. **Parallel Plugin Load Time** (需求 18.2)
   - Target: 10 plugins < 200ms
   - Measures: Parallel loading of multiple plugins

3. **Tool Call Dispatch Time** (需求 18.3)
   - Target: < 10ms per dispatch
   - Measures: Tool lookup and validation time

4. **Plugin Memory Usage** (需求 18.5)
   - Target: < 5MB per plugin
   - Note: Requires manual verification with Task Manager

### Expected Performance Results

```
[Benchmark] Single Plugin Load Time
Requirement: < 50ms per plugin

  ✓ PASS WorkspaceToolsPlugin.dll: 25ms
  ✓ PASS ShellToolsPlugin.dll: 18ms
  ✓ PASS WebToolsPlugin.dll: 32ms
  ✓ PASS MemoryToolsPlugin.dll: 28ms
  ✓ PASS SchedulerToolsPlugin.dll: 22ms

Statistics:
  Min:     18ms
  Max:     32ms
  Average: 25ms
  Target:  < 50ms

[Benchmark] Parallel Plugin Load Time
Requirement: 10 plugins < 200ms

  Plugins tested: 5
  Average time:   120ms
  Target:         < 200ms
  Result:         ✓ PASS

[Benchmark] Tool Call Dispatch Time
Requirement: < 10ms per dispatch

  Iterations:  1000
  Min:         50μs
  Max:         500μs
  Average:     150μs (0.15ms)
  Target:      < 10ms (10000μs)
  Result:      ✓ PASS
```

---

## Troubleshooting

### Common Issues

#### Issue 1: Test Executable Not Found

**Symptom**: Script reports "Executable not found"

**Solution**:
1. Verify the test was built successfully
2. Check the build output directory
3. Rebuild the test if necessary

#### Issue 2: Plugin Not Found

**Symptom**: Tests fail with "Plugin not found" error

**Solution**:
1. Verify plugins are built: `ls ../../build-plugins/release/plugins/`
2. Check plugin paths in test configuration
3. Rebuild plugins if necessary

#### Issue 3: Test Failures

**Symptom**: Some tests fail

**Solution**:
1. Review test output for error messages
2. Check if plugins are compatible with SDK version
3. Verify all dependencies are installed
4. Run tests with `-Verbose` flag for detailed output

#### Issue 4: Performance Tests Fail

**Symptom**: Performance benchmarks exceed targets

**Solution**:
1. Close other applications to free resources
2. Run tests on a clean system
3. Check if running in Debug mode (should use Release)
4. Verify hardware meets minimum requirements

---

## Acceptance Criteria

### Task 29.1: Complete Test Suite

- [ ] All unit tests pass (100%)
- [ ] All integration tests pass (100%)
- [ ] No critical failures
- [ ] Test coverage > 80%

### Task 29.2: Manual Testing

- [ ] All tool plugins work correctly
- [ ] All backend plugins work correctly
- [ ] Tool inter-calling works
- [ ] Delegate and teammate functionality works
- [ ] No functionality lost compared to pre-migration

### Task 29.3: Performance Benchmarks

- [ ] Single plugin load < 50ms
- [ ] 10 plugins parallel load < 200ms
- [ ] Tool call dispatch < 10ms
- [ ] Plugin memory usage < 5MB

### Overall Acceptance

- [ ] All automated tests pass
- [ ] All manual tests pass
- [ ] All performance benchmarks meet targets
- [ ] No critical issues found
- [ ] Documentation is complete

---

## Reporting

### Test Report Structure

1. **Executive Summary**
   - Overall pass/fail status
   - Key findings
   - Recommendations

2. **Test Results**
   - Detailed results for each test
   - Pass/fail statistics
   - Performance metrics

3. **Issues Found**
   - List of all issues
   - Severity classification
   - Recommended fixes

4. **Recommendations**
   - Next steps
   - Areas for improvement
   - Risk assessment

### Report Template

Use the generated report: `REGRESSION_TEST_REPORT_<timestamp>.md`

Or create a custom report using this structure:

```markdown
# TmAgent Plugin SDK - Regression Test Report

## Executive Summary
- Date: YYYY-MM-DD
- Tester: [Name]
- Overall Status: PASS / FAIL / CONDITIONAL PASS

## Test Results
### Task 29.1: Complete Test Suite
- Unit Tests: X/Y passed
- Integration Tests: X/Y passed

### Task 29.2: Manual Testing
- Tool Plugins: X/Y passed
- Backend Plugins: X/Y passed
- Functionality: X/Y passed

### Task 29.3: Performance Benchmarks
- Plugin Load Time: PASS / FAIL
- Tool Dispatch Time: PASS / FAIL
- Memory Usage: PASS / FAIL

## Issues Found
| # | Description | Severity | Status |
|---|-------------|----------|--------|
| 1 | ...         | High     | Open   |

## Recommendations
- ...
```

---

## Next Steps

After completing all regression tests:

1. **If all tests pass**:
   - Mark Task 29 as complete
   - Proceed to Task 30 (Checkpoint - 官方插件迁移验收)
   - Prepare for Phase 4 (文档和生态)

2. **If some tests fail**:
   - Document all failures
   - Prioritize fixes by severity
   - Fix critical issues
   - Re-run failed tests
   - Update test report

3. **If performance targets not met**:
   - Profile performance bottlenecks
   - Optimize critical paths
   - Re-run performance benchmarks
   - Document any trade-offs

---

## References

- Requirements Document: `.kiro/specs/plugin-sdk-architecture/requirements.md`
- Design Document: `.kiro/specs/plugin-sdk-architecture/design.md`
- Tasks Document: `.kiro/specs/plugin-sdk-architecture/tasks.md`
- Phase 2 Checkpoint Results: `PHASE2_CHECKPOINT_RESULTS.md`

---

## Contact

For questions or issues with the regression tests, please contact:
- Project Lead: [Name]
- QA Lead: [Name]
- Development Team: [Email]

---

**Document Version**: 1.0  
**Last Updated**: 2026-04-01  
**Status**: Active
