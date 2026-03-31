# Minimal Backend Plugin Example

This is a minimal example demonstrating how to create a backend plugin using the TmAgent Plugin SDK.

## Features

- Implements a simple delegate backend
- Demonstrates session management
- Shows callback mechanism usage
- Includes both qmake and CMake build configurations

## Building

### Using qmake

```bash
qmake MinimalBackendPlugin.pro
make
```

### Using CMake

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Plugin Structure

- `MinimalBackendPlugin.h/cpp` - Plugin entry point implementing `IBackendPlugin`
- `MinimalDelegateBackend` - Delegate backend implementing `IDelegateBackend`
- `MinimalDelegateSession` - Session implementation for task execution
- `minimal_backend.json` - Plugin metadata file

## Features

### Delegate Backend

The plugin provides a simple delegate backend that:
- Accepts task descriptions
- Simulates task execution with callbacks
- Supports cancellation
- Returns simple completion messages

### Callbacks

The backend demonstrates all callback types:
- `onActivity` - Notifies when processing starts
- `onSummary` - Provides task summary
- `onSuccess` - Called on successful completion
- `onFailure` - Called on cancellation or errors

## Limitations

This is a minimal example and does not:
- Implement actual AI model integration
- Support teammate backend mode
- Perform real task execution
- Handle complex error scenarios

For production backends, refer to the official backend plugins (codex, tmagent).
