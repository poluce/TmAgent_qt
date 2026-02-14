#include "LLMProvider.h"
#include <QNetworkAccessManager>
#include <QTimer>

LLMProvider::LLMProvider(QObject* parent)
    : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
}

LLMProvider::~LLMProvider()
{
}
