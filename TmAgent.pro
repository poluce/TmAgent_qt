TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += \
    backend_plugin_codex \
    backend_plugin_tmagent \
    tool_plugin_workspace \
    tool_plugin_shell \
    tool_plugin_codeintel \
    tool_plugin_web \
    tool_plugin_memory \
    tool_plugin_coordination \
    tmagent_app \
    tmagent_cli \
    tmagent_log

backend_plugin_codex.file = src/plugins/backends/codex/CodexBackendPlugin.pro
backend_plugin_tmagent.file = src/plugins/backends/tmagent/TmagentBackendPlugin.pro
tool_plugin_workspace.file = src/plugins/tools/workspace/WorkspaceToolsPlugin.pro
tool_plugin_shell.file = src/plugins/tools/shell/ShellToolsPlugin.pro
tool_plugin_codeintel.file = src/plugins/tools/codeintel/CodeIntelToolsPlugin.pro
tool_plugin_web.file = src/plugins/tools/web/WebToolsPlugin.pro
tool_plugin_memory.file = src/plugins/tools/memory/MemoryToolsPlugin.pro
tool_plugin_coordination.file = src/plugins/tools/coordination/CoordinationToolsPlugin.pro
tmagent_app.file = app.pro
tmagent_cli.file = cli.pro
tmagent_log.file = logcli.pro
