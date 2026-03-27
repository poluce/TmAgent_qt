# src/ui/ui.pri — UI 层

INCLUDEPATH += \
    $$PWD \
    $$PWD/views \
    $$PWD/dialogs \
    $$PWD/support \
    $$PWD/workbench \
    $$PWD/widgets

SOURCES += \
    $$PWD/support/AgentLifecycleSupport.cpp \
    $$PWD/support/ChatListUiSupport.cpp \
    $$PWD/support/ChatUiFlowSupport.cpp \
    $$PWD/support/ComponentInspectSupport.cpp \
    $$PWD/dialogs/CommandPolicyDialog.cpp \
    $$PWD/dialogs/InformationSettingsDialog.cpp \
    $$PWD/dialogs/McpConfigDialog.cpp \
    $$PWD/dialogs/ModelConfigDialog.cpp \
    $$PWD/dialogs/ToolPluginDialog.cpp \
    $$PWD/support/ProfileUiSupport.cpp \
    $$PWD/support/SessionUiSupport.cpp \
    $$PWD/support/ToolLogUiSupport.cpp \
    $$PWD/workbench/ToolLogWidget.cpp \
    $$PWD/views/MainWindow.cpp \
    $$PWD/views/IdentityView.cpp \
    $$PWD/support/HistoryUiSupport.cpp \
    $$PWD/workbench/HistoryFormatters.cpp \
    $$PWD/dialogs/AgentCreateDialog.cpp \
    $$PWD/support/AvatarUtils.cpp \
    $$PWD/widgets/ThinkingIndicatorWidget.cpp \
    $$PWD/widgets/ToolPermissionEditor.cpp

HEADERS += \
    $$PWD/support/AgentLifecycleSupport.h \
    $$PWD/support/ChatListUiSupport.h \
    $$PWD/support/ConversationEventUiSupport.h \
    $$PWD/support/ChatUiFlowSupport.h \
    $$PWD/support/ComponentInspectSupport.h \
    $$PWD/dialogs/CommandPolicyDialog.h \
    $$PWD/dialogs/InformationSettingsDialog.h \
    $$PWD/dialogs/McpConfigDialog.h \
    $$PWD/dialogs/ModelConfigDialog.h \
    $$PWD/dialogs/ToolPluginDialog.h \
    $$PWD/support/ProfileUiSupport.h \
    $$PWD/support/SessionUiSupport.h \
    $$PWD/support/ToolLogUiSupport.h \
    $$PWD/workbench/ToolLogWidget.h \
    $$PWD/views/MainWindow.h \
    $$PWD/views/IdentityView.h \
    $$PWD/support/HistoryUiSupport.h \
    $$PWD/workbench/HistoryFormatters.h \
    $$PWD/dialogs/AgentCreateDialog.h \
    $$PWD/support/AvatarUtils.h \
    $$PWD/widgets/ThinkingIndicatorWidget.h \
    $$PWD/widgets/ToolPermissionEditor.h


