QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = SchedulerToolTest

SOURCES += \
    SchedulerToolTest.cpp \
    ../../src/plugins/tools/scheduler/SchedulerTool.cpp

HEADERS += \
    ../../src/core/tools/SchedulerTool.h \
    ../../src/core/agent/ToolTypes.h \
    ../../src/core/tools/ToolSchemaSupport.h

INCLUDEPATH += ../../src ../../src/core/service/include ../../src/core/tools
