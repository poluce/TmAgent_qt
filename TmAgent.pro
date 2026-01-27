QT       += core gui network widgets
INCLUDEPATH += src

# 第三方库
include(3rdparty/yaml-cpp.pri)
include(3rdparty/tree-sitter.pri)

# QChatWidget 子模块（源码直引）
# chat_widget.pri 已带入 theme_manager、qss_utils、styles.qrc。chatlist 仅追加源码与头文件，避免重复引入导致 Makefile 重复目标。
INCLUDEPATH += $$PWD/QChatWidget/src
include($$PWD/QChatWidget/src/chatwidget/chat_widget.pri)
CHATLIST_DIR = $$PWD/QChatWidget/src/chatlist
INCLUDEPATH += $$CHATLIST_DIR
SOURCES += $$CHATLIST_DIR/chat_list_delegate.cpp \
    $$CHATLIST_DIR/chat_list_filter_model.cpp \
    $$CHATLIST_DIR/chat_list_view.cpp \
    $$CHATLIST_DIR/chat_list_widget.cpp
HEADERS += $$CHATLIST_DIR/chat_list_roles.h \
    $$CHATLIST_DIR/chat_list_delegate.h \
    $$CHATLIST_DIR/chat_list_filter_model.h \
    $$CHATLIST_DIR/chat_list_view.h \
    $$CHATLIST_DIR/chat_list_widget.h
MODELCONFIG_DIR = $$PWD/QChatWidget/src/modelconfig
INCLUDEPATH += $$MODELCONFIG_DIR
HEADERS += $$MODELCONFIG_DIR/model_config_import_page.h
SOURCES += $$MODELCONFIG_DIR/model_config_import_page.cpp

TARGET = TmAgent
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated.
DEFINES += QT_DEPRECATED_WARNINGS

CONFIG += c++17
# 明确告知 qmake 项目包含 C 源码，md4c 为 C 文件
CONFIG += c

SOURCES += \
    src/main.cpp \
    src/newCore/ModelRouter.cpp \
    src/newCore/ModelFactory.cpp \
    src/core/agent/LLMAgent.cpp \
    src/core/agent/OpenAICompatibleClient.cpp \
    src/core/agent/ToolDispatcher.cpp \
    src/core/agent/ToolRegistry.cpp \
    src/core/utils/AppSettings.cpp \
    src/core/utils/ToolSchemaLoader.cpp \
    src/core/parser/TreeSitterParser.cpp \
    src/core/lsp/JsonRpcTransport.cpp \
    src/core/lsp/LspClient.cpp \
    src/core/lsp/LspServerManager.cpp \
    src/core/lsp/LspDownloader.cpp \
    src/core/lsp/BuildSystemAdapter.cpp \
    src/ui/AgentChatWidget.cpp \
    src/ui/ToolLogWidget.cpp \
    src/core/tools/AgentTool.cpp

HEADERS += \
    src/newCore/LLMTypes.h \
    src/newCore/LLMProvider.h \
    src/newCore/ModelRouter.h \
    src/newCore/ModelFactory.h \
    src/core/agent/LLMAgent.h \
    src/core/agent/ILLMClient.h \
    src/core/agent/OpenAICompatibleClient.h \
    src/core/agent/ToolDispatcher.h \
    src/core/agent/ToolRegistry.h \
    src/core/utils/AppSettings.h \
    src/core/utils/ToolSchemaLoader.h \
    src/core/parser/TreeSitterParser.h \
    src/core/lsp/JsonRpcTransport.h \
    src/core/lsp/LspClient.h \
    src/core/lsp/LspProtocol.h \
    src/core/lsp/LspServerManager.h \
    src/core/lsp/LspDownloader.h \
    src/core/lsp/BuildSystemAdapter.h \
    src/ui/AgentChatWidget.h \
    src/ui/ToolLogWidget.h \
    src/core/agent/AgentEventBus.h \
    src/core/agent/ToolTypes.h \
    src/core/tools/FileOperationTools.h \
    src/core/tools/FileTool.h \
    src/core/tools/ShellTool.h \
    src/core/tools/CodeParserTool.h \
    src/core/tools/LspTool.h \
    src/core/tools/WebTool.h \
    src/core/tools/ExternalSearchTool.h \
    src/core/tools/PatchTool.h \
    src/core/tools/BuiltinTools.h \
    src/core/tools/ToolRegistrationHelpers.h \
    src/core/tools/AgentTool.h \
    src/core/utils/ToolSchemaLoader.h

# FORMS += \
#    src/ui/LLMConfigWidget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target.path

# 自动复制 resources 目录到构建输出目录
win32 {
    RESOURCES_SRC_DIR = $$replace(PWD, /, \\)\\resources
    OPENSSL_SRC_DIR = $$replace(PWD, /, \\)\\openssl
    BUILD_DEST_DIR = $$replace(OUT_PWD, /, \\)

    CONFIG(debug, debug|release) {
        QMAKE_POST_LINK += xcopy /Y /E /I \"$$RESOURCES_SRC_DIR\" \"$$BUILD_DEST_DIR\\debug\\resources\" &
        QMAKE_POST_LINK += copy /Y \"$$OPENSSL_SRC_DIR\\*.dll\" \"$$BUILD_DEST_DIR\\debug\\\"
    } else {
        QMAKE_POST_LINK += xcopy /Y /E /I \"$$RESOURCES_SRC_DIR\" \"$$BUILD_DEST_DIR\\release\\resources\" &
        QMAKE_POST_LINK += copy /Y \"$$OPENSSL_SRC_DIR\\*.dll\" \"$$BUILD_DEST_DIR\\release\\\"
    }
}



