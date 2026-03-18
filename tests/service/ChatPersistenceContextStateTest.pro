QT += core sql
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ChatPersistenceContextStateTest

SOURCES += ChatPersistenceContextStateTest.cpp \
    ../../src/core/persistence/ChatPersistenceService.cpp \
    ../../src/core/persistence/DatabaseManager.cpp \
    ../../src/core/service/ConversationContextTypes.cpp \
    ../../src/core/model/IdentityProfile.cpp

HEADERS += ../../src/core/persistence/ChatPersistenceService.h \
    ../../src/core/model/Message.h \
    ../../src/core/model/IdentityProfile.h \
    ../../src/llm/LLMTypes.h \
    ../../src/core/service/include/ConversationContextTypes.h

INCLUDEPATH += ../../src ../../src/core/service/include
