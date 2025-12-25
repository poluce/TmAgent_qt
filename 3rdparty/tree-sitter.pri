# tree-sitter 增量解析库集成
# 核心库版本: v0.26.3
# C++语法版本: v0.23.4
# GitHub: https://github.com/tree-sitter/tree-sitter

TREE_SITTER_ROOT = $$PWD/tree-sitter-0.26.3
TREE_SITTER_CPP_ROOT = $$PWD/tree-sitter-cpp-0.23.4

# 启用 MinGW 的 C99 格式说明符支持（解决 %zu 警告）
win32-g++: DEFINES += __USE_MINGW_ANSI_STDIO=1

# 头文件路径
INCLUDEPATH += $$TREE_SITTER_ROOT/lib/include
INCLUDEPATH += $$TREE_SITTER_ROOT/lib/src      # 内部头文件依赖
INCLUDEPATH += $$TREE_SITTER_CPP_ROOT/src      # tree_sitter/parser.h 依赖

# 核心库源码 - 只需编译 lib.c，它会 include 其他 .c 文件
SOURCES += $$TREE_SITTER_ROOT/lib/src/lib.c

# C++ 语法定义 - 使用代理包装文件避免与 yaml-cpp 的文件名冲突
# 包装文件位于独立的 wrappers 目录，保持 3rdparty 目录整洁
SOURCES += $$PWD/wrappers/ts_cpp_parser_wrapper.c
SOURCES += $$PWD/wrappers/ts_cpp_scanner_wrapper.c
