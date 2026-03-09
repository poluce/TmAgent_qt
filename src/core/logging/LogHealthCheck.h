#ifndef LOGHEALTHCHECK_H
#define LOGHEALTHCHECK_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace LogHealthCheck {

struct HealthStatus {
    bool healthy = true;
    bool canWrite = false;
    bool canRead = false;
    qint64 diskFreeBytes = -1;
    qint64 logDirSizeBytes = 0;
    int eventFileCount = 0;
    int sessionCount = 0;
    QStringList issues;
};

// 执行健康检查
HealthStatus check(const QString& dataRootPath = QString());

// 格式化输出
QString formatReport(const HealthStatus& status);
QJsonObject toJson(const HealthStatus& status);

} // namespace LogHealthCheck

#endif // LOGHEALTHCHECK_H
