QT += core
CONFIG += console c++17
TEMPLATE = app
TARGET = TreeSitterParserTest

# 项目根目录的相对路径（从 tests/parser/ 出发）
INCLUDEPATH += ../../src

# 复用主工程的 tree-sitter 配置，确保宏/源文件一致
include(../../3rdparty/tree-sitter.pri)

SOURCES += \
    TreeSitterParserTest.cpp \
    ../../src/core/parser/TreeSitterParser.cpp

HEADERS += \
    ../../src/core/parser/TreeSitterParser.h


