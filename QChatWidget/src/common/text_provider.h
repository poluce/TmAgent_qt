#ifndef TEXT_PROVIDER_H
#define TEXT_PROVIDER_H

#include <QCoreApplication>
#include <QHash>
#include <QString>

/**
 * @brief 统一 UI 文本可配置机制
 *
 * 库内部通过 QCHAT_TEXT("key", tr("默认值")) 获取文本，
 * 宿主通过 TextProvider::instance().setText("key", "自定义") 覆盖。
 * 未设置覆盖时返回 defaultValue，保持向后兼容。
 */
class TextProvider {
public:
    static TextProvider& instance();

    void setText(const QString& key, const QString& value);
    void setTexts(const QHash<QString, QString>& texts);
    void clearOverrides();
    QString text(const QString& key, const QString& defaultValue) const;

private:
    TextProvider() = default;
    TextProvider(const TextProvider&) = delete;
    TextProvider& operator=(const TextProvider&) = delete;

    QHash<QString, QString> m_overrides;
};

#define QCHAT_TEXT(key, defaultVal) TextProvider::instance().text(QStringLiteral(key), defaultVal)

#endif // TEXT_PROVIDER_H
