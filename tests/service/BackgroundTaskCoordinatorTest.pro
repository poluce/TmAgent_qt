QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = BackgroundTaskCoordinatorTest

SOURCES += \
    BackgroundTaskCoordinatorTest.cpp \
    ../../src/core/service/background/BackgroundTaskCoordinator.cpp \
    ../../src/core/model/Identity.cpp \
    ../../src/core/model/IdentityProfile.cpp

HEADERS += \
    ../../src/core/service/include/BackgroundTaskCoordinator.h \
    ../../src/core/service/include/CoordinatorContext.h \
    ../../src/core/model/Identity.h \
    ../../src/core/model/IdentityProfile.h \
    ../../src/core/model/Message.h

INCLUDEPATH += ../../src ../../src/core/service/include
