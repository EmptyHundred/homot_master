# HMScript Sandbox Tests

This directory contains GDScript test cases for the HMScript sandbox security system.

## Test Structure

Each test is a single `.hm` file:
- `test_name.hm` - The test script with a `test()` function (HMScript file)

Tests pass if they complete without throwing errors.

## Test Categories

### Security Tests
- **sandbox_blocked_classes.hm** - Tests that dangerous classes (OS, FileAccess, DirAccess, Thread) are blocked
- **sandbox_blocked_methods.hm** - Tests that dangerous methods (Object.call, ClassDB.instantiate, etc.) are blocked
- **sandbox_path_validation.hm** - Tests path whitelist and traversal attack prevention

### Resource Management Tests
- **sandbox_execution_timeout.hm** - Tests execution time limits
- **sandbox_api_rate_limiting.hm** - Tests per-frame API operation limits
- **sandbox_resource_limits.hm** - Tests memory allocation limits
- **sandbox_frame_reset.hm** - Tests that per-frame counters reset correctly

### Operation Tests
- **sandbox_safe_operations.hm** - Tests that safe operations (math, strings, basic nodes) work correctly
- **sandbox_property_access.hm** - Tests property get/set security checks
- **sandbox_api_categories.hm** - Tests READ/WRITE/HEAVY operation categorization

### Integration Tests
- **sandbox_inheritance_check.hm** - Tests that blocked methods are blocked on derived classes
- **sandbox_context_isolation.hm** - Tests that different sandbox profiles don't interfere
- **sandbox_nested_calls.hm** - Tests that security checks work with nested method calls
- **sandbox_callable_restrictions.hm** - Tests callable and signal restrictions

### Error Handling Tests
- **sandbox_error_reporting.hm** - Tests error aggregation and reporting

## Running Tests

These tests are integrated with Godot's test framework and can be run using:

```bash
# Run all hmscript tests
godot --test --source modules/hmscript/tests/scripts

# Run specific test
godot --test --source modules/hmscript/tests/scripts/sandbox_blocked_classes.hm

# Or use the test runner
godot --headless --script modules/hmscript/tests/scripts/tester.gd
```

## Design Reference

These tests are based on the sandbox design documented in `modules/hmscript/design.md`, which describes:

1. **SandboxConfig** - Class/method/property blocklists and path validation
2. **ExecutionLimiter** - Timeout, memory, and API rate limiting
3. **SafeWrapper** - Unified entry point for all sandbox API calls
4. **ErrorRegistry** - Error aggregation and reporting
5. **SandboxProfile** - Per-script/context sandbox configuration

## Note

Some tests currently only demonstrate the expected behavior. The actual sandbox enforcement is implemented in the C++ layer (`modules/hmscript/sandbox/`) and hooked into the GDScript VM as described in the design document.
