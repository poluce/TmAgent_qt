#include "text_provider.h"

TextProvider& TextProvider::instance()
{
    static TextProvider provider;
    return provider;
}

void TextProvider::setText(const QString& key, const QString& value)
{
    m_overrides.insert(key, value);
}

void TextProvider::setTexts(const QHash<QString, QString>& texts)
{
    for (auto it = texts.constBegin(); it != texts.constEnd(); ++it) {
        m_overrides.insert(it.key(), it.value());
    }
}

void TextProvider::clearOverrides()
{
    m_overrides.clear();
}

QString TextProvider::text(const QString& key, const QString& defaultValue) const
{
    auto it = m_overrides.constFind(key);
    if (it != m_overrides.constEnd()) {
        return it.value();
    }
    return defaultValue;
}
