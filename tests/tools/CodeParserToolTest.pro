# CodeParserTool 测试项目

QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = CodeParserToolTest

# 源文件
SOURCES += CodeParserToolTest.cpp \
           ../../src/core/parser/TreeSitterParser.cpp

# 包含路径
INCLUDEPATH += ../../src
INCLUDEPATH += ../../3rdparty/tree-sitter-0.26.3/lib/include

# 依赖库
include(../../3rdparty/tree-sitter.pri)
