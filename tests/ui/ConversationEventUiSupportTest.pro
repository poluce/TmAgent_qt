TEMPLATE = app
TARGET = ConversationEventUiSupportTest
QT += testlib core gui widgets
CONFIG += console testcase c++17

INCLUDEPATH += ../../src ../../src/core/service/include

SOURCES += \
    ConversationEventUiSupportTest.cpp

HEADERS += \
    ConversationEventUiSupportTest.h \
    ../../src/ui/support/ConversationEventUiSupport.h \
    ../../src/core/agent/ToolTypes.h



