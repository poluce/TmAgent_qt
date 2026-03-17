QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = DelegateSettlementCoordinatorTest

SOURCES += \
    DelegateSettlementCoordinatorTest.cpp \
    ../../src/core/service/background/DelegateSettlementCoordinator.cpp

HEADERS += \
    ../../src/core/service/include/DelegateSettlementCoordinator.h \
    ../../src/core/model/Message.h

INCLUDEPATH += ../../src ../../src/core/service/include




