TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += \
    backend_plugin_codex \
    backend_plugin_tmagent \
    tmagent_app \
    tmagent_cli \
    tmagent_log

backend_plugin_codex.file = plugins/backends/codex/CodexBackendPlugin.pro
backend_plugin_tmagent.file = plugins/backends/tmagent/TmagentBackendPlugin.pro
tmagent_app.file = app.pro
tmagent_cli.file = cli.pro
tmagent_log.file = tmagent-log/tmagent-log.pro
