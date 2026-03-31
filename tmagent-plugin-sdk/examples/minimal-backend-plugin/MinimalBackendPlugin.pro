QT += core
QT -= gui

TEMPLATE = lib
CONFIG += plugin c++17
TARGET = MinimalBackendPlugin

# Include SDK
TMAGENT_SDK_PATH = $$PWD/../..
include($$TMAGENT_SDK_PATH/tmagent-plugin-sdk.pri)

SOURCES += \
    MinimalBackendPlugin.cpp

HEADERS += \
    MinimalBackendPlugin.h

# Output directory
CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/debug
} else {
    DESTDIR = $$OUT_PWD/release
}

# Copy metadata file
QMAKE_POST_LINK += $$QMAKE_COPY $$PWD/minimal_backend.json $$DESTDIR
