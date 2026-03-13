TEMPLATE = app
TARGET = HistoryFormattersTest
QT += testlib core gui widgets
CONFIG += console testcase c++17

INCLUDEPATH += ../../src

SOURCES += \
    HistoryFormattersTest.cpp \
    ../../src/core/service/ExecutionHistoryModel.cpp \
    ../../src/ui/HistoryFormatters.cpp

HEADERS += \
    HistoryFormattersTest.h \
    ../../src/core/service/ExecutionHistoryModel.h \
    ../../src/ui/HistoryFormatters.h
