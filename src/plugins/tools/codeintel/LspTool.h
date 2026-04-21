#ifndef LSPTOOL_H
#define LSPTOOL_H

#include <tmagent/types/ToolTypes.h>
#include "core/lsp/LspProtocol.h"
#include <QJsonObject>
#include <QList>
#include <QString>

class LspClient;

/**
 * @brief LSP 代理工具
 *
 * 仿照 opencode 的万能 LSP 工具设计。
 */
class LspTool {
public:
    static constexpr const char* LSP = "lsp";
    static TmAgent::Tool toolSchema();

    /**
     * @brief 执行 LSP 操作
     * @param input {operation, file_path, line, character}
     */
    static QString execute(const QJsonObject& input);

    friend class LspToolTest;

private:
    static bool isCppFile(const QString& filePath);
    static void scheduleRetry(const QJsonObject& input, const QString& toolCallId);
    static QString formatLocations(const QList<Lsp::Location>& locs);
};

#endif // LSPTOOL_H
