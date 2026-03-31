# src/core/logging/logging.pri — Logging / observability shared module

SOURCES += \
    $$PWD/LogQueryEngine.cpp \
    $$PWD/LogRecordSupport.cpp \
    $$PWD/LogCatalog.cpp \
    $$PWD/LogFollower.cpp \
    $$PWD/LogIndex.cpp \
    $$PWD/LogHealthCheck.cpp \
    $$PWD/../observability/AlertManager.cpp \
    $$PWD/../observability/MetricsCollector.cpp

HEADERS += \
    $$PWD/LogQueryEngine.h \
    $$PWD/LogCatalog.h \
    $$PWD/LogFollower.h \
    $$PWD/LogIndex.h \
    $$PWD/LogHealthCheck.h \
    $$PWD/../observability/AlertManager.h \
    $$PWD/../observability/MetricsCollector.h
