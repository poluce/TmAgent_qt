# cli.pro — Headless CLI runner (no GUI)
QT += core network sql
QT -= gui

INCLUDEPATH += src \
               src/cli

CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = TmAgentCli

DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += TMAGENT_CLI   # 用于条件编译排除 GUI 相关代码

# ─── 第三方库 ───
include(3rdparty/yaml-cpp.pri)
include(3rdparty/tree-sitter.pri)
include(3rdparty/qtkeychain/qtkeychain.pri)

# ─── LLM 层 ───
include(src/llm/llm.pri)

# ─── Core 完整模块（含 ChatService / Manager / Memory / Persistence） ───
include(src/core/core.pri)

# ─── CLI 入口 ───
SOURCES += src/cli/cli_main.cpp src/cli/CliRunner.cpp src/cli/InteractiveCli.cpp src/cli/CodexInteractiveCli.cpp src/plugins/backends/codex/CodexAppServerClient.cpp
HEADERS += src/cli/CliRunner.h src/cli/InteractiveCli.h src/cli/CodexInteractiveCli.h src/plugins/backends/codex/CodexAppServerClient.h

# ─── 部署 ───
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target.path

# 自动复制运行时资源到构建输出目录，插件统一进入 resources/plugins
win32 {
    RESOURCES_SRC_DIR = $$replace(PWD, /, \\)\\resources
    BUILD_DEST_DIR = $$replace(OUT_PWD, /, \\)
    RUNTIME_COPY_SCRIPT = $$replace(PWD, /, \\)\\scripts\\copy_runtime_assets.cmd

    CONFIG(debug, debug|release) {
        BACKEND_PLUGIN_SRC_DIR = $$replace(PWD, /, \\)\\build-plugins\\debug\\plugins\\backends
        BACKEND_PLUGIN_DEST_DIR = $$BUILD_DEST_DIR\\debug\\resources\\plugins\\backends
        TOOL_PLUGIN_SRC_DIR = $$replace(PWD, /, \\)\\build-plugins\\debug\\plugins\\tools
        TOOL_PLUGIN_DEST_DIR = $$BUILD_DEST_DIR\\debug\\resources\\plugins\\tools
        QMAKE_POST_LINK += cmd /c \"\"$$RUNTIME_COPY_SCRIPT\" \"$$RESOURCES_SRC_DIR\" \"$$BUILD_DEST_DIR\\debug\\resources\" \"$$BACKEND_PLUGIN_SRC_DIR\" \"$$BACKEND_PLUGIN_DEST_DIR\" \"$$TOOL_PLUGIN_SRC_DIR\" \"$$TOOL_PLUGIN_DEST_DIR\"\"
    } else {
        BACKEND_PLUGIN_SRC_DIR = $$replace(PWD, /, \\)\\build-plugins\\release\\plugins\\backends
        BACKEND_PLUGIN_DEST_DIR = $$BUILD_DEST_DIR\\release\\resources\\plugins\\backends
        TOOL_PLUGIN_SRC_DIR = $$replace(PWD, /, \\)\\build-plugins\\release\\plugins\\tools
        TOOL_PLUGIN_DEST_DIR = $$BUILD_DEST_DIR\\release\\resources\\plugins\\tools
        QMAKE_POST_LINK += cmd /c \"\"$$RUNTIME_COPY_SCRIPT\" \"$$RESOURCES_SRC_DIR\" \"$$BUILD_DEST_DIR\\release\\resources\" \"$$BACKEND_PLUGIN_SRC_DIR\" \"$$BACKEND_PLUGIN_DEST_DIR\" \"$$TOOL_PLUGIN_SRC_DIR\" \"$$TOOL_PLUGIN_DEST_DIR\"\"
    }
}

# 解决 cc1plus.exe out of memory 问题
QMAKE_CXXFLAGS += -Wa,-mbig-obj
CONFIG(release, debug|release) {
    QMAKE_CXXFLAGS += -O2
} else {
    QMAKE_CXXFLAGS += -O0
}
