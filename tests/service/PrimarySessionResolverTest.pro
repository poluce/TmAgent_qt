QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = PrimarySessionResolverTest

SOURCES += \
    PrimarySessionResolverTest.cpp \
    ../../src/core/manager/SessionManager.cpp \
    ../../src/core/model/Session.cpp \
    ../../src/core/model/Identity.cpp \
    ../../src/core/model/IdentityProfile.cpp \
    IdentityManagerEnqueueStub.cpp

HEADERS += \
    ../../src/core/service/include/PrimarySessionResolver.h \
    ../../src/core/manager/SessionManager.h \
    ../../src/core/model/Session.h \
    ../../src/core/model/Identity.h \
    ../../src/core/model/IdentityProfile.h

INCLUDEPATH += ../../src ../../src/core/service/include




