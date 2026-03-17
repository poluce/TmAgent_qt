QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = SchedulerTriggerCoordinatorTest

SOURCES += \
    SchedulerTriggerCoordinatorTest.cpp \
    ../../src/core/service/background/SchedulerTriggerCoordinator.cpp \
    ../../src/core/model/Identity.cpp \
    ../../src/core/model/IdentityProfile.cpp

HEADERS += \
    ../../src/core/service/include/SchedulerTriggerCoordinator.h \
    ../../src/core/model/Identity.h \
    ../../src/core/model/IdentityProfile.h

INCLUDEPATH += ../../src ../../src/core/service/include




