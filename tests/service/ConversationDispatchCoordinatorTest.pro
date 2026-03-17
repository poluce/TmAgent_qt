QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = ConversationDispatchCoordinatorTest

SOURCES += \
    ConversationDispatchCoordinatorTest.cpp \
    ../../src/core/service/conversation/ConversationDispatchCoordinator.cpp \
    ../../src/core/model/Session.cpp \
    ../../src/core/model/Identity.cpp \
    ../../src/core/model/IdentityProfile.cpp

HEADERS += \
    ../../src/core/service/include/ConversationDispatchCoordinator.h \
    ../../src/core/service/include/TurnManager.h \
    ../../src/core/model/Session.h \
    ../../src/core/model/Identity.h \
    ../../src/core/model/IdentityProfile.h \
    ../../src/core/model/Message.h

INCLUDEPATH += ../../src ../../src/core/service/include




