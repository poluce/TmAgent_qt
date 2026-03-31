# Minimal Tool Plugin Example

This is a minimal example demonstrating how to create a tool plugin using the TmAgent Plugin SDK.

## Features

- Implements a simple `echo` tool that returns the input message
- Demonstrates SDK interface usage
- Shows proper error handling
- Includes both qmake and CMake build configurations

## Building

### Using qmake

```bash
qmake MinimalToolPlugin.pro
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

- `MinimalToolPlugin.h/cpp` - Plugin entry point implementing `IToolPlugin`
- `MinimalToolProvider` - Tool provider implementing `IToolProvider`
- `minimal_tool.json` - Plugin metadata file

## Usage

The plugin provides one tool:

### echo

Echoes back the input message.

**Parameters:**
- `message` (string, required): The message to echo

**Example:**
```json
{
  "name": "echo",
  "input": {
    "message": "Hello, World!"
  }
}
```

**Response:**
```
Hello, World!
```
