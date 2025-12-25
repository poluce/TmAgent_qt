#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QString>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>

/**
 * @brief 应用配置持久化类
 * 
 * 负责将配置读写到 config.ini 文件
 */
class AppSettings {
public:
    static void setApiKey(const QString& key);
    static QString getApiKey();

    static void setBaseUrl(const QString& url);
    static QString getBaseUrl();

    static void setModel(const QString& model);
    static QString getModel();

    static void setSystemPrompt(const QString& prompt);
    static QString getSystemPrompt();

    static void setTemperature(double temp);
    static double getTemperature();

private:
    static QSettings& settings();
};

#endif // APPSETTINGS_H
