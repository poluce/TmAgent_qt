#include "LogIndex.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace LogIndex {

namespace {

// 从 JSON 对象中提取时间戳（毫秒），与 LogQueryEngine 保持一致
qint64 extractTimestampMs(const QJsonObject& obj)
{
    const QStringList keys = {
        QStringLiteral("timestamp"),
        QStringLiteral("time"),
        QStringLiteral("createdAt"),
        QStringLiteral("created_at")
    };

    for (const QString& key : keys) {
        const QString ts = obj.value(key).toString().trimmed();
        if (ts.isEmpty())
            continue;

        QDateTime dt = QDateTime::fromString(ts, Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(ts, Qt::ISODate);
        if (!dt.isValid())
            continue;

        if (dt.timeSpec() == Qt::LocalTime)
            dt = dt.toUTC();
        return dt.toMSecsSinceEpoch();
    }
    return -1;
}

// 从 JSON 对象中提取 level 字符串
QString extractLevel(const QJsonObject& obj)
{
    const QString level = obj.value(QStringLiteral("level")).toString().trimmed().toLower();
    if (!level.isEmpty())
        return level;
    return obj.value(QStringLiteral("severity")).toString().trimmed().toLower();
}

// 从 JSON 对象中提取 success 信息
void extractSuccess(const QJsonObject& obj, bool* known, bool* value)
{
    *known = false;
    *value = false;

    if (obj.contains(QStringLiteral("success"))
        && obj.value(QStringLiteral("success")).isBool()) {
        *known = true;
        *value = obj.value(QStringLiteral("success")).toBool();
        return;
    }

    const QJsonObject toolEvent = obj.value(QStringLiteral("toolEvent")).toObject();
    if (toolEvent.contains(QStringLiteral("success"))
        && toolEvent.value(QStringLiteral("success")).isBool()) {
        *known = true;
        *value = toolEvent.value(QStringLiteral("success")).toBool();
    }
}

// 将 IndexEntry 写入 raw 字节（小端序，平台无关）
QByteArray serializeEntry(const IndexEntry& entry)
{
    QByteArray buf;
    buf.resize(32);
    QDataStream stream(&buf, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << entry.timestampMs
           << entry.fileOffset
           << entry.lineLength
           << entry.flags
           << entry.reserved;
    return buf;
}

// 从 raw 字节反序列化 IndexEntry
IndexEntry deserializeEntry(const QByteArray& buf)
{
    IndexEntry entry{};
    QDataStream stream(buf);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream >> entry.timestampMs
           >> entry.fileOffset
           >> entry.lineLength
           >> entry.flags
           >> entry.reserved;
    return entry;
}

// 统计 JSONL 文件的非空行数
int countJsonlLines(const QString& jsonlPath)
{
    QFile file(jsonlPath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return -1;

    int count = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (!line.isEmpty())
            ++count;
    }
    return count;
}

} // anonymous namespace

// ── 公开 API ────────────────────────────────────────────────

QString indexPathForJsonl(const QString& jsonlPath)
{
    if (jsonlPath.endsWith(QStringLiteral(".jsonl"), Qt::CaseInsensitive))
        return jsonlPath.left(jsonlPath.size() - 6) + QStringLiteral(".idx");
    return jsonlPath + QStringLiteral(".idx");
}

bool appendEntry(const QString& indexPath, const IndexEntry& entry)
{
    QFile file(indexPath);
    if (!file.open(QFile::Append))
        return false;

    const QByteArray data = serializeEntry(entry);
    const qint64 written = file.write(data);
    if (written != 32)
        return false;

    file.flush();
    return true;
}

QVector<IndexEntry> readIndex(const QString& indexPath)
{
    QVector<IndexEntry> entries;

    QFile file(indexPath);
    if (!file.exists() || !file.open(QFile::ReadOnly))
        return entries;

    const qint64 fileSize = file.size();
    const int entryCount = static_cast<int>(fileSize / 32);
    entries.reserve(entryCount);

    for (int i = 0; i < entryCount; ++i) {
        const QByteArray buf = file.read(32);
        if (buf.size() != 32)
            break;
        entries.append(deserializeEntry(buf));
    }
    return entries;
}

bool rebuildIndex(const QString& jsonlPath)
{
    QFile jsonlFile(jsonlPath);
    if (!jsonlFile.open(QFile::ReadOnly | QFile::Text))
        return false;

    const QString idxPath = indexPathForJsonl(jsonlPath);

    // 使用 QSaveFile 确保原子写入
    QSaveFile idxFile(idxPath);
    if (!idxFile.open(QFile::WriteOnly))
        return false;

    qint64 offset = 0;
    while (!jsonlFile.atEnd()) {
        const QByteArray line = jsonlFile.readLine();
        const QByteArray trimmed = line.trimmed();

        if (!trimmed.isEmpty()) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);

            IndexEntry entry{};
            entry.fileOffset = offset;
            entry.lineLength = static_cast<quint32>(line.size());
            entry.reserved = 0;

            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject obj = doc.object();
                entry.timestampMs = extractTimestampMs(obj);
                entry.flags = encodeLevelFlag(extractLevel(obj));

                bool known = false;
                bool value = false;
                extractSuccess(obj, &known, &value);
                if (known)
                    entry.flags |= SuccessKnownBit;
                if (value)
                    entry.flags |= SuccessBit;
            } else {
                entry.timestampMs = -1;
                entry.flags = 0;
            }

            const QByteArray data = serializeEntry(entry);
            if (idxFile.write(data) != 32) {
                idxFile.cancelWriting();
                return false;
            }
        }

        offset += line.size();
    }

    return idxFile.commit();
}

bool validateIndex(const QString& jsonlPath)
{
    const QString idxPath = indexPathForJsonl(jsonlPath);

    QFile idxFile(idxPath);
    if (!idxFile.exists())
        return false;
    if (!idxFile.open(QFile::ReadOnly))
        return false;

    const qint64 idxSize = idxFile.size();
    idxFile.close();

    if (idxSize % 32 != 0)
        return false;

    const int indexEntryCount = static_cast<int>(idxSize / 32);
    const int lineCount = countJsonlLines(jsonlPath);

    if (lineCount < 0)
        return false;

    return indexEntryCount == lineCount;
}

quint32 encodeLevelFlag(const QString& level)
{
    const QString lower = level.trimmed().toLower();
    if (lower == QLatin1String("warning"))
        return LevelWarning;
    if (lower == QLatin1String("error"))
        return LevelError;
    if (lower == QLatin1String("debug"))
        return LevelDebug;
    return LevelInfo;
}

QString decodeLevelFlag(quint32 flags)
{
    switch (flags & LevelMask) {
    case LevelWarning: return QStringLiteral("warning");
    case LevelError:   return QStringLiteral("error");
    case LevelDebug:   return QStringLiteral("debug");
    default:           return QStringLiteral("info");
    }
}

IndexEntry buildEntryFromJson(const QJsonObject& obj, qint64 fileOffset, quint32 lineLength)
{
    IndexEntry entry{};
    entry.timestampMs = extractTimestampMs(obj);
    entry.fileOffset = fileOffset;
    entry.lineLength = lineLength;
    entry.reserved = 0;

    entry.flags = encodeLevelFlag(extractLevel(obj));

    bool known = false;
    bool value = false;
    extractSuccess(obj, &known, &value);
    if (known)
        entry.flags |= SuccessKnownBit;
    if (value)
        entry.flags |= SuccessBit;

    return entry;
}

} // namespace LogIndex
