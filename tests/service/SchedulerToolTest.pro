QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = SchedulerToolTest

SOURCES += \
    SchedulerToolTest.cpp \
    ../../src/core/tools/SchedulerTool.cpp

HEADERS += \
    ../../src/core/tools/SchedulerTool.h \
    ../../src/core/agent/ToolTypes.h \
    ../../src/core/tools/ToolSchemaSupport.h

INCLUDEPATH += ../../src ../../src/core/service/include
