TEMPLATE = app
TARGET = HistoryFormattersTest
QT += testlib core gui widgets
CONFIG += console testcase c++17

INCLUDEPATH += ../../src ../../src/core/service/include

SOURCES += \
    HistoryFormattersTest.cpp \
    ../../src/core/service/observability/ExecutionHistoryModel.cpp \
    ../../src/ui/workbench/HistoryFormatters.cpp

HEADERS += \
    HistoryFormattersTest.h \
    ../../src/core/service/include/ExecutionHistoryModel.h \
    ../../src/ui/workbench/HistoryFormatters.h




