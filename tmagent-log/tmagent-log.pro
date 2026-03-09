QT += core sql
TEMPLATE = app
TARGET = tmagent-log

CONFIG += c++17 console
CONFIG -= app_bundle

INCLUDEPATH += ../src

SOURCES += \
    main.cpp \
    ../src/core/persistence/DatabaseManager.cpp \
    ../src/core/logging/LogQueryEngine.cpp \
    ../src/core/logging/LogDbUtils.cpp \
    ../src/core/logging/LogDbScanner.cpp \
    ../src/core/logging/LogFieldExtractor.cpp \
    ../src/core/logging/LogSummarizer.cpp \
    ../src/core/logging/LogFormatter.cpp \
    ../src/core/logging/LogScanner.cpp \
    ../src/core/logging/LogSessionLister.cpp \
    ../src/core/logging/LogAgentLister.cpp \
    ../src/core/logging/LogFollower.cpp

HEADERS += \
    ../src/core/persistence/DatabaseManager.h \
    ../src/core/logging/LogQueryEngine.h \
    ../src/core/logging/LogDbUtils.h \
    ../src/core/logging/LogDbScanner.h \
    ../src/core/logging/LogFieldExtractor.h \
    ../src/core/logging/LogSummarizer.h \
    ../src/core/logging/LogFormatter.h \
    ../src/core/logging/LogScanner.h \
    ../src/core/logging/LogSessionLister.h \
    ../src/core/logging/LogAgentLister.h \
    ../src/core/logging/LogFollower.h
