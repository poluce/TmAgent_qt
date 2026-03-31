QT += core
QT -= gui

TEMPLATE = lib
CONFIG += plugin c++17
TARGET = MinimalToolPlugin

# Include SDK
TMAGENT_SDK_PATH = $$PWD/../..
include($$TMAGENT_SDK_PATH/tmagent-plugin-sdk.pri)

SOURCES += \
    MinimalToolPlugin.cpp

HEADERS += \
    MinimalToolPlugin.h

# Output directory
CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/debug
} else {
    DESTDIR = $$OUT_PWD/release
}

# Copy metadata file
QMAKE_POST_LINK += $$QMAKE_COPY $$PWD/minimal_tool.json $$DESTDIR
