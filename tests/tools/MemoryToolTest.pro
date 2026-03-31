# MemoryTool 测试项目

QT += core sql
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = MemoryToolTest

# 源文件
SOURCES += MemoryToolTest.cpp \
           ../../src/plugins/tools/memory/MemoryTool.cpp

# 包含路径
INCLUDEPATH += ../../src ../../src/core/tools


