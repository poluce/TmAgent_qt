# src/core/core.pri — Core 聚合模块（App 完整版）
# 先引入 CLI/App 共享的基础模块
include($$PWD/core-base.pri)

# App 独有的子模块
include($$PWD/model/model.pri)
include($$PWD/service/service.pri)

# memory
SOURCES += \
    $$PWD/memory/MemoryDocument.cpp \
    $$PWD/memory/MemoryManager.cpp
HEADERS += \
    $$PWD/memory/MemoryDocument.h \
    $$PWD/memory/MemoryManager.h

# manager
SOURCES += \
    $$PWD/manager/IdentityManager.cpp \
    $$PWD/manager/SessionManager.cpp
HEADERS += \
    $$PWD/manager/IdentityManager.h \
    $$PWD/manager/SessionManager.h

# persistence
SOURCES += \
    $$PWD/persistence/ChatPersistenceService.cpp
HEADERS += \
    $$PWD/persistence/ChatPersistenceService.h
