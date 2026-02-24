QT += testlib widgets dbus
CONFIG += qt warn_on depend_includepath testcase link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += libsecret-1

TEMPLATE = app
TARGET = tst_keychain

SOURCES += tst_keychain.cpp

INCLUDEPATH += ../src
INCLUDEPATH += ../3rdparty/qtkeychain/qtkeychain

# 定义宏
DEFINES += QTKEYCHAIN_NO_EXPORT LIBSECRET_SUPPORT KEYCHAIN_DBUS HAVE_LIBSECRET

# 链接主项目的对象文件
BUILD_DIR = ../build

OBJECTS += \
    $$BUILD_DIR/KeychainHelper.o \
    $$BUILD_DIR/keychain.o \
    $$BUILD_DIR/keychain_unix.o \
    $$BUILD_DIR/plaintextstore.o \
    $$BUILD_DIR/gnomekeyring.o \
    $$BUILD_DIR/libsecret.o \
    $$BUILD_DIR/kwallet_interface.o \
    $$BUILD_DIR/moc_keychain_p.o \
    $$BUILD_DIR/moc_keychain.o \
    $$BUILD_DIR/moc_gnomekeyring_p.o \
    $$BUILD_DIR/moc_kwallet_interface.o
