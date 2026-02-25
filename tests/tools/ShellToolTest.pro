# ShellTool 测试项目

QT += core widgets

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = ShellToolTest

# 源文件
SOURCES += ShellToolTest.cpp \
    ../../src/core/tools/ShellTool.cpp

# 包含路径
INCLUDEPATH += ../../src
