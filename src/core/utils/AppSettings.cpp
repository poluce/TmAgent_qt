#include "AppSettings.h"

QSettings& AppSettings::settings() {
    static QSettings s(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
    static bool initialized = false;
    if (!initialized) {
        s.setIniCodec("UTF-8");  // 使用 UTF-8 编码，支持中文
        initialized = true;
    }
    return s;
}

void AppSettings::setApiKey(const QString& key) {
    settings().setValue("llm/api_key", key);
    settings().sync();
}

QString AppSettings::getApiKey() {
    return settings().value("llm/api_key", "").toString();
}

void AppSettings::setBaseUrl(const QString& url) {
    settings().setValue("llm/base_url", url);
    settings().sync();
}

QString AppSettings::getBaseUrl() {
    // 默认预设 DeepSeek 官方地址
    return settings().value("llm/base_url", "https://api.deepseek.com").toString();
}

void AppSettings::setModel(const QString& model) {
    settings().setValue("llm/model", model);
    settings().sync();
}

QString AppSettings::getModel() {
    // 默认预设 DeepSeek Chat 模型
    return settings().value("llm/model", "deepseek-chat").toString();
}

void AppSettings::setSystemPrompt(const QString& prompt) {
    settings().setValue("llm/system_prompt", prompt);
    settings().sync();
}

QString AppSettings::getSystemPrompt() {
    return settings().value("llm/system_prompt", "你是一个专业的 Qt 高级开发工程师，旨在帮助用户解决 C++/Qt 相关的编程问题。").toString();
}

void AppSettings::setTemperature(double temp) {
    settings().setValue("llm/temperature", temp);
    settings().sync();
}

double AppSettings::getTemperature() {
    return settings().value("llm/temperature", 0.7).toDouble();
}
