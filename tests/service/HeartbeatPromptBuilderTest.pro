QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = HeartbeatPromptBuilderTest

SOURCES += \
    HeartbeatPromptBuilderTest.cpp

HEADERS += \
    ../../src/core/service/include/HeartbeatPromptBuilder.h

INCLUDEPATH += ../../src ../../src/core/service/include




