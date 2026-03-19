QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = ConversationContextServiceTest

SOURCES += \
    ConversationContextServiceTest.cpp \
    ../../src/core/service/conversation/ConversationContextService.cpp

HEADERS += \
    ../../src/core/service/include/ConversationContextService.h \
    ../../src/core/service/include/ConversationContextTypes.h \
    ../../src/core/service/include/TurnManager.h

INCLUDEPATH += ../../src ../../src/core/service/include
