CHATWIDGET_DIR = $$PWD
MD4C_DIR = $$CHATWIDGET_DIR/../../3rdparty/md4c

INCLUDEPATH += \
    $$CHATWIDGET_DIR \
    $$MD4C_DIR

SOURCES += \
    $$CHATWIDGET_DIR/chat_widget_model.cpp \
    $$CHATWIDGET_DIR/chat_widget_delegate.cpp \
    $$CHATWIDGET_DIR/chat_widget_view.cpp \
    $$CHATWIDGET_DIR/chat_widget_input.cpp \
    $$CHATWIDGET_DIR/chat_widget.cpp \
    $$CHATWIDGET_DIR/chat_widget_command.cpp \
    $$CHATWIDGET_DIR/chat_widget_markdown_utils.cpp \
    $$MD4C_DIR/md4c.c \
    $$MD4C_DIR/md4c-html.c \
    $$MD4C_DIR/entity.c

HEADERS += \
    $$CHATWIDGET_DIR/chat_widget_model.h \
    $$CHATWIDGET_DIR/chat_widget_delegate.h \
    $$CHATWIDGET_DIR/chat_widget_view.h \
    $$CHATWIDGET_DIR/chat_widget_input.h \
    $$CHATWIDGET_DIR/chat_widget.h \
    $$CHATWIDGET_DIR/chat_widget_command.h \
    $$CHATWIDGET_DIR/chat_widget_markdown_utils.h \
    $$MD4C_DIR/md4c.h \
    $$MD4C_DIR/md4c-html.h \
    $$MD4C_DIR/entity.h

STYLES_QRC = $$clean_path($$PWD/../../resources/styles.qrc)
isEmpty(QCHAT_STYLES_QRC_INCLUDED) {
    QCHAT_STYLES_QRC_INCLUDED = 1
    RESOURCES += $$STYLES_QRC
}

include($$PWD/../common/theme_manager.pri)
include($$PWD/../common/qss_utils.pri)
include($$PWD/../common/text_provider.pri)
