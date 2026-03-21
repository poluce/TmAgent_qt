QT += core network sql
QT -= gui

CONFIG += c++17 console
CONFIG += c
CONFIG -= app_bundle

TEMPLATE = app
TARGET = BackendPluginManagerTest

INCLUDEPATH += ../../src ../../src/core/service/include

include(../../3rdparty/yaml-cpp.pri)
include(../../3rdparty/tree-sitter.pri)
include(../../3rdparty/qtkeychain/qtkeychain.pri)
include(../../src/llm/llm.pri)
include(../../src/core/core.pri)

SOURCES += BackendPluginManagerTest.cpp

win32 {
    BUILD_DEST_DIR = $$replace(OUT_PWD, /, \\)

    CONFIG(debug, debug|release) {
        BACKEND_PLUGIN_SRC_DIR = ..\\..\\..\\build-plugins\\debug\\plugins\\backends
        BACKEND_PLUGIN_DEST_DIR = $$BUILD_DEST_DIR\\debug\\plugins\\backends
        QMAKE_POST_LINK += if exist \"$$BACKEND_PLUGIN_DEST_DIR\" rmdir /S /Q \"$$BACKEND_PLUGIN_DEST_DIR\" &
        QMAKE_POST_LINK += mkdir \"$$BACKEND_PLUGIN_DEST_DIR\" &
        QMAKE_POST_LINK += if exist \"$$BACKEND_PLUGIN_SRC_DIR\\*.dll\" xcopy /Y /I \"$$BACKEND_PLUGIN_SRC_DIR\\*.dll\" \"$$BACKEND_PLUGIN_DEST_DIR\\\"
    } else {
        BACKEND_PLUGIN_SRC_DIR = ..\\..\\..\\build-plugins\\release\\plugins\\backends
        BACKEND_PLUGIN_DEST_DIR = $$BUILD_DEST_DIR\\release\\plugins\\backends
        QMAKE_POST_LINK += if exist \"$$BACKEND_PLUGIN_DEST_DIR\" rmdir /S /Q \"$$BACKEND_PLUGIN_DEST_DIR\" &
        QMAKE_POST_LINK += mkdir \"$$BACKEND_PLUGIN_DEST_DIR\" &
        QMAKE_POST_LINK += if exist \"$$BACKEND_PLUGIN_SRC_DIR\\*.dll\" xcopy /Y /I \"$$BACKEND_PLUGIN_SRC_DIR\\*.dll\" \"$$BACKEND_PLUGIN_DEST_DIR\\\"
    }
}
