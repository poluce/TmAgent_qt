# TmAgent Plugin SDK

A lightweight, header-only SDK for developing TmAgent plugins. Build tool plugins and backend plugins without depending on the main application source code.

## Features

- **Lightweight**: < 100KB, header-only library
- **Minimal Dependencies**: Only requires Qt Core
- **ABI Stable**: Semantic versioning with backward compatibility
- **Cross-Platform**: Windows, Linux, macOS support
- **Flexible Build**: Both qmake and CMake support
- **Well Documented**: Complete API reference and tutorials

## Quick Start

### Installation

#### Option 1: Include in Your Project

```bash
git clone https://github.com/tmagent/tmagent-plugin-sdk.git
```

#### Option 2: System-wide Installation (CMake)

```bash
cd tmagent-plugin-sdk
mkdir build && cd build
cmake ..
sudo cmake --install .
```

### Creating Your First Plugin

#### Using qmake

1. Create a new Qt plugin project:

```qmake
# MyPlugin.pro
QT += core
TEMPLATE = lib
CONFIG += plugin c++17

# Include SDK
TMAGENT_SDK_PATH = /path/to/tmagent-plugin-sdk
include($$TMAGENT_SDK_PATH/tmagent-plugin-sdk.pri)

SOURCES += MyPlugin.cpp
HEADERS += MyPlugin.h
```

2. Implement the plugin:

```cpp
#include <tmagent/plugin/IToolPlugin.h>
#include <tmagent/plugin/IToolProvider.h>

class MyPlugin : public QObject, public TmAgent::IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "my_plugin.json")
    Q_INTERFACES(TmAgent::IToolPlugin)
    
public:
    TmAgent::ToolPluginDescriptor descriptor() const override;
    TmAgent::IToolProvider* createProvider(TmAgent::IToolPluginHost* host, 
                                          QObject* parent) override;
};
```

#### Using CMake

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(MyPlugin)

find_package(Qt5 COMPONENTS Core REQUIRED)
find_package(tmagent-plugin-sdk 1.0 REQUIRED)

add_library(MyPlugin MODULE MyPlugin.cpp)
target_link_libraries(MyPlugin PRIVATE 
    Qt5::Core 
    tmagent-plugin-sdk
)
```

## Plugin Types

### Tool Plugins

Provide tools that AI agents can call to perform actions:

- File system operations
- Shell command execution
- Web requests
- Code analysis
- Custom business logic

See [examples/minimal-tool-plugin](examples/minimal-tool-plugin) for a complete example.

### Backend Plugins

Provide AI model backends for delegation and collaboration:

- **Delegate Backend**: For sub-task delegation
- **Teammate Backend**: For persistent collaboration

See [examples/minimal-backend-plugin](examples/minimal-backend-plugin) for a complete example.

## Documentation

- [API Reference](docs/API.md) - Complete interface documentation
- [Tutorial](docs/TUTORIAL.md) - Step-by-step plugin development guide
- [Migration Guide](docs/MIGRATION.md) - Migrating from old plugin system

## Examples

- [Minimal Tool Plugin](examples/minimal-tool-plugin) - Simple echo tool
- [Minimal Backend Plugin](examples/minimal-backend-plugin) - Basic delegate backend

## Requirements

- Qt 5.15+ or Qt 6.2+
- C++17 compiler
- CMake 3.16+ or qmake

## Versioning

This SDK follows [Semantic Versioning](https://semver.org/):

- **MAJOR**: ABI-incompatible changes
- **MINOR**: ABI-compatible feature additions
- **PATCH**: Bug fixes and documentation updates

Current version: **1.0.0**

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Support

- Documentation: [docs/](docs/)
- Issues: https://github.com/tmagent/tmagent-plugin-sdk/issues
- Discussions: https://github.com/tmagent/tmagent-plugin-sdk/discussions

## Contributing

Contributions are welcome! Please read our contributing guidelines before submitting pull requests.
