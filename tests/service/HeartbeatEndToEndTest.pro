# Heartbeat 端到端验收项目

QT += core network sql
QT -= gui

CONFIG += c++17 console
CONFIG += c
CONFIG -= app_bundle

TEMPLATE = app
TARGET = HeartbeatEndToEndTest

INCLUDEPATH += ../../src ../../src/core/service/include

include(../../3rdparty/yaml-cpp.pri)
include(../../3rdparty/tree-sitter.pri)
include(../../3rdparty/qtkeychain/qtkeychain.pri)
include(../../src/llm/llm.pri)
include(../../src/core/core.pri)

SOURCES += HeartbeatEndToEndTest.cpp



