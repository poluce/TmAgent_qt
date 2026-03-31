TEMPLATE = app
TARGET = HistoryFormattersTest
QT += testlib core gui widgets
CONFIG += console testcase c++17

INCLUDEPATH += ../../src

SOURCES += \
    HistoryFormattersTest.cpp \
    ../../src/ui/workbench/HistoryFormatters.cpp

HEADERS += \
    HistoryFormattersTest.h \
    ../../src/ui/workbench/HistoryFormatters.h




