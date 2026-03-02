# src/llm/llm.pri — LLM 提供者层

SOURCES += \
    $$PWD/LLMProvider.cpp \
    $$PWD/ModelFactory.cpp \
    $$PWD/OpenAICompatibleProvider.cpp \
    $$PWD/AnthropicProvider.cpp

HEADERS += \
    $$PWD/LLMTypes.h \
    $$PWD/LLMProvider.h \
    $$PWD/ModelFactory.h \
    $$PWD/OpenAICompatibleProvider.h \
    $$PWD/AnthropicProvider.h
