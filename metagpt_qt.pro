QT       += core gui network widgets
INCLUDEPATH += src

TARGET = metagpt_qt
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated.
DEFINES += QT_DEPRECATED_WARNINGS

CONFIG += c++17

SOURCES += \
    src/main.cpp \
    src/core/agent/LLMAgent.cpp \
    src/core/utils/ConfigManager.cpp \
    src/ui/LLMConfigWidget.cpp

HEADERS += \
    src/core/agent/LLMAgent.h \
    src/core/utils/ConfigManager.h \
    src/ui/LLMConfigWidget.h

# FORMS += \
#    src/ui/LLMConfigWidget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target.path

# 自动处理 Windows 下的 OpenSSL DLL 拷贝
win32 {
    # 处理路径中的斜杠以适应 Windows 命令行
    OPENSSL_SRC_DIR = $$replace(PWD, /, \\)\\openssl
    BUILD_DEST_DIR = $$replace(OUT_PWD, /, \\)

    # 根据配置 (Debug/Release) 拷贝到对应的子目录
    CONFIG(debug, debug|release) {
        # QMAKE_POST_LINK += xcopy /Y /D /I \"$$OPENSSL_SRC_DIR\\*.dll\" \"$$BUILD_DEST_DIR\\debug\\\"
    } else {
        # QMAKE_POST_LINK += xcopy /Y /D /I \"$$OPENSSL_SRC_DIR\\*.dll\" \"$$BUILD_DEST_DIR\\release\\\"
    }
}
