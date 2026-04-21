QT += core testlib
QT -= gui

CONFIG += qt console warn_on depend_includepath testcase c++17
CONFIG -= app_bundle

TEMPLATE = app

# Include SDK
TMAGENT_SDK_PATH = $$PWD/../../tmagent-plugin-sdk
include($$TMAGENT_SDK_PATH/tmagent-plugin-sdk.pri)

# Include core headers
INCLUDEPATH += $$PWD/../../src

SOURCES += \
    PluginLoadingIntegrationTest.cpp

HEADERS += \
    PluginLoadingIntegrationTest.h

# Link against core library (if needed)
# LIBS += -L$$OUT_PWD/../../src/core -lcore

TARGET = PluginLoadingIntegrationTest
