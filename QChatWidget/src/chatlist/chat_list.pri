CHATLIST_DIR = $$PWD

INCLUDEPATH += $$CHATLIST_DIR

SOURCES += \
    $$CHATLIST_DIR/chat_list_delegate.cpp \
    $$CHATLIST_DIR/chat_list_filter_model.cpp \
    $$CHATLIST_DIR/chat_list_view.cpp \
    $$CHATLIST_DIR/chat_list_widget.cpp

HEADERS += \
    $$CHATLIST_DIR/chat_list_roles.h \
    $$CHATLIST_DIR/chat_list_delegate.h \
    $$CHATLIST_DIR/chat_list_filter_model.h \
    $$CHATLIST_DIR/chat_list_view.h \
    $$CHATLIST_DIR/chat_list_widget.h

STYLES_QRC = $$clean_path($$PWD/../../resources/styles.qrc)
isEmpty(QCHAT_STYLES_QRC_INCLUDED) {
    QCHAT_STYLES_QRC_INCLUDED = 1
    RESOURCES += $$STYLES_QRC
}

include($$PWD/../common/theme_manager.pri)
include($$PWD/../common/qss_utils.pri)
include($$PWD/../common/text_provider.pri)
