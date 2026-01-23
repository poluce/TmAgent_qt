#ifndef LSPINSTALLTOOL_H
#define LSPINSTALLTOOL_H

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSet>
#include "core/lsp/LspServerManager.h"
#include "core/agent/AgentEventBus.h"
#include "core/agent/ToolTypes.h"

/**
 * @brief LSP 安装/下载工具（目前仅支持 clangd）
 */
class LspInstallTool {
public:
    static QString execute(const QJsonObject& input) {
        QString language = input["language"].toString().trimmed();
        if (language.isEmpty()) {
            language = "cpp";
        }

        const QString toolCallId = input["_tool_call_id"].toString();
        if (language != "cpp") {
            return "错误: 仅支持 cpp (clangd)";
        }

        LspServerManager *mgr = LspServerManager::instance();
        const QString clangdPath = mgr->clangdPath();
        if (!clangdPath.isEmpty()) {
            return QString("已安装 clangd: %1").arg(clangdPath);
        }

        if (toolCallId.isEmpty()) {
            return "错误: 缺少工具调用 ID，无法回传下载结果";
        }

        scheduleInstall(toolCallId);
        return makeDeferredToolResult("clangd 下载中，完成后自动返回结果");
    }

private:
    static void scheduleInstall(const QString &toolCallId) {
        static QSet<QString> scheduled;
        if (scheduled.contains(toolCallId)) return;
        scheduled.insert(toolCallId);

        LspServerManager::instance()->addClangdWaiter([toolCallId](bool success, const QString &path) {
            QString result;
            if (!success || path.isEmpty()) {
                result = "错误: clangd 下载失败";
            } else {
                result = QString("已安装 clangd: %1").arg(path);
            }
            scheduled.remove(toolCallId);
            AgentEventBus::instance()->postToolResult(toolCallId, result);
        });
    }
};

#endif // LSPINSTALLTOOL_H
