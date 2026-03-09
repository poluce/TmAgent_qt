#ifndef LOGINDEX_H
#define LOGINDEX_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace LogIndex {

// 索引条目（固定 32 字节，便于二进制读写）
#pragma pack(push, 1)
struct IndexEntry {
    qint64 timestampMs;    // 8 bytes — 事件时间戳（毫秒）
    qint64 fileOffset;     // 8 bytes — 行在 JSONL 文件中的字节偏移
    quint32 lineLength;    // 4 bytes — 行的字节长度
    quint32 flags;         // 4 bytes — 位标志（低2位=level编码, bit2=success, bit3=successKnown）
    quint64 reserved;      // 8 bytes — 预留扩展
};
#pragma pack(pop)

static_assert(sizeof(IndexEntry) == 32, "IndexEntry must be exactly 32 bytes");

// Level 编码（存储在 flags 低2位）
enum LevelFlag : quint32 {
    LevelInfo    = 0,
    LevelWarning = 1,
    LevelError   = 2,
    LevelDebug   = 3,
    LevelMask    = 0x03
};

// flags 位定义
enum FlagBit : quint32 {
    SuccessBit      = 0x04,  // bit2: success 值
    SuccessKnownBit = 0x08,  // bit3: success 是否已知
};

// 从 JSONL 文件路径推导索引文件路径（.jsonl → .idx）
QString indexPathForJsonl(const QString& jsonlPath);

// 追加一条索引记录
bool appendEntry(const QString& indexPath, const IndexEntry& entry);

// 读取整个索引文件
QVector<IndexEntry> readIndex(const QString& indexPath);

// 从 JSONL 文件重建索引
bool rebuildIndex(const QString& jsonlPath);

// 验证索引是否与 JSONL 文件一致（检查条目数 vs 行数）
bool validateIndex(const QString& jsonlPath);

// 编码 level 字符串为 flags 位
quint32 encodeLevelFlag(const QString& level);

// 解码 flags 位为 level 字符串
QString decodeLevelFlag(quint32 flags);

// 从已解析的 JSON 对象构建 IndexEntry（便捷函数，供写入侧集成）
IndexEntry buildEntryFromJson(const QJsonObject& obj, qint64 fileOffset, quint32 lineLength);

} // namespace LogIndex

#endif // LOGINDEX_H
