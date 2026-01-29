#ifndef KEYCHAINHELPER_H
#define KEYCHAINHELPER_H

#include <QString>

namespace KeychainHelper {

QString serviceName();
QString entryIdForModel(const QString& provider, const QString& modelId);
QString makeKeyRef(const QString& entryId);
bool parseKeyRef(const QString& value, QString* entryId);

QString readPasswordSync(const QString& entryId, bool* ok = nullptr, QString* error = nullptr);
bool writePasswordSync(const QString& entryId, const QString& secret, QString* error = nullptr);

} // namespace KeychainHelper

#endif // KEYCHAINHELPER_H
