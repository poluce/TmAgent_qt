QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = ConversationEnqueueCoordinatorTest

SOURCES += \
    ConversationEnqueueCoordinatorTest.cpp \
    ../../src/core/service/conversation/ConversationEnqueueCoordinator.cpp \
    ../../src/core/service/conversation/MessageRouter.cpp \
    IdentityManagerEnqueueStub.cpp \
    ../../src/core/manager/SessionManager.cpp \
    ../../src/core/model/Session.cpp \
    ../../src/core/model/Identity.cpp \
    ../../src/core/model/IdentityProfile.cpp

HEADERS += \
    ../../src/core/service/include/ConversationEnqueueCoordinator.h \
    ../../src/core/service/include/MessageRouter.h \
    ../../src/core/service/include/TurnManager.h \
    ../../src/core/manager/SessionManager.h \
    ../../src/core/model/Session.h \
    ../../src/core/model/Identity.h \
    ../../src/core/model/IdentityProfile.h \
    ../../src/core/model/Message.h

INCLUDEPATH += ../../src ../../src/core/service/include




