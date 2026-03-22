QT += core network sql
QT -= gui

CONFIG += c++17 console
CONFIG += c
CONFIG -= app_bundle

TEMPLATE = app
TARGET = ToolPluginSchemaSmokeTest

INCLUDEPATH += ../../src ../../src/core/service/include

include(../../3rdparty/yaml-cpp.pri)
include(../../3rdparty/tree-sitter.pri)
include(../../3rdparty/qtkeychain/qtkeychain.pri)
include(../../src/llm/llm.pri)
include(../../src/core/core.pri)

SOURCES += ToolPluginSchemaSmokeTest.cpp

win32 {
    BUILD_DEST_DIR = $$replace(OUT_PWD, /, \\)
    RUNTIME_COPY_SCRIPT = ..\\..\\..\\scripts\\copy_runtime_assets.cmd

    CONFIG(debug, debug|release) {
        BACKEND_PLUGIN_SRC_DIR = ..\\..\\..\\build-plugins\\debug\\plugins\\backends
        BACKEND_PLUGIN_DEST_DIR = $$BUILD_DEST_DIR\\debug\\resources\\plugins\\backends
        TOOL_PLUGIN_SRC_DIR = ..\\..\\..\\build-plugins\\debug\\plugins\\tools
        TOOL_PLUGIN_DEST_DIR = $$BUILD_DEST_DIR\\debug\\resources\\plugins\\tools
        QMAKE_POST_LINK += cmd /c \"\"$$RUNTIME_COPY_SCRIPT\" \"-\" \"-\" \"$$BACKEND_PLUGIN_SRC_DIR\" \"$$BACKEND_PLUGIN_DEST_DIR\" \"$$TOOL_PLUGIN_SRC_DIR\" \"$$TOOL_PLUGIN_DEST_DIR\"\"
    } else {
        BACKEND_PLUGIN_SRC_DIR = ..\\..\\..\\build-plugins\\release\\plugins\\backends
        BACKEND_PLUGIN_DEST_DIR = $$BUILD_DEST_DIR\\release\\resources\\plugins\\backends
        TOOL_PLUGIN_SRC_DIR = ..\\..\\..\\build-plugins\\release\\plugins\\tools
        TOOL_PLUGIN_DEST_DIR = $$BUILD_DEST_DIR\\release\\resources\\plugins\\tools
        QMAKE_POST_LINK += cmd /c \"\"$$RUNTIME_COPY_SCRIPT\" \"-\" \"-\" \"$$BACKEND_PLUGIN_SRC_DIR\" \"$$BACKEND_PLUGIN_DEST_DIR\" \"$$TOOL_PLUGIN_SRC_DIR\" \"$$TOOL_PLUGIN_DEST_DIR\"\"
    }
}
