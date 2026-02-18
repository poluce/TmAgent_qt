#ifndef LSPTOOL_H
#define LSPTOOL_H

#include "core/agent/AgentEventBus.h"
#include "core/agent/ToolTypes.h"
#include "core/lsp/LspClient.h"
#include "core/lsp/LspProtocol.h"
#include "core/lsp/LspServerManager.h"
#include <QDebug>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

/**
 * @brief LSP 代理工具
 *
 * 仿照 opencode 的万能 LSP 工具设计。
 */
class LspTool {
public:
    static constexpr const char* LSP = "lsp";

    /**
     * @brief 执行 LSP 操作
     * @param input {operation, file_path, line, character}
     */
    static QString execute(const QJsonObject& input)
    {
        QString operation = input["operation"].toString();
        QString filePath = input["file_path"].toString();
        QString toolCallId = input["_tool_call_id"].toString();

        if (operation == "status") {
            QJsonObject status;
            LspServerManager* mgr = LspServerManager::instance();
            QString clangdPath = mgr->clangdPath();
            status["clangd_available"] = !clangdPath.isEmpty();
            status["clangd_path"] = clangdPath;
            status["download_in_progress"] = mgr->isClangdDownloadInProgress();
            return QString::fromUtf8(QJsonDocument(status).toJson(QJsonDocument::Compact));
        }

        if (!filePath.isEmpty()) {
            QFileInfo fi(filePath);
            if (fi.isRelative()) {
                filePath = fi.absoluteFilePath();
            }
            filePath = Lsp::normalizePath(filePath);
        }
        // Agent 传进来的是 0-based，符合底层逻辑
        int line = input["line"].toInt();
        int character = input["character"].toInt();

        LspClient* client = LspServerManager::instance()->getClientForFile(filePath);
        if (!client) {
            if (isCppFile(filePath) && !LspServerManager::instance()->isClangdAvailable()) {
                if (!toolCallId.isEmpty()) {
                    scheduleRetry(input, toolCallId);
                }
                return makeDeferredToolResult("clangd 缺失，已在后台下载，完成后自动重试");
            }
            return "错误: 无法为该文件启动 LSP 服务器";
        }

        if (!client->isReady()) {
            QEventLoop initLoop;
            QTimer initTimer;
            initTimer.setSingleShot(true);
            QString initError;
            QObject::connect(&initTimer, &QTimer::timeout, &initLoop, &QEventLoop::quit);
            QObject::connect(client, &LspClient::initialized, &initLoop, &QEventLoop::quit);
            QObject::connect(client, &LspClient::errorOccurred, &initLoop, [&](const QString& err) {
                initError = err;
                initLoop.quit();
            });
            initTimer.start(5000);
            initLoop.exec();
            if (!client->isReady()) {
                return initError.isEmpty()
                    ? "错误: LSP 服务器初始化超时"
                    : ("错误: LSP 服务器初始化失败: " + initError);
            }
        }

        const bool needsFile = (operation != "workspaceSymbol");
        if (needsFile) {
            if (filePath.isEmpty()) {
                return "错误: 缺少 file_path";
            }
            if (!client->ensureDocumentOpen(filePath)) {
                return "错误: 无法打开文档或发送 didOpen";
            }
        }

        QString resultStr;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(8000); // 增加到 8 秒，因为有些请求涉及多步

        if (operation == "goToDefinition") {
            client->requestDefinition(filePath, line, character, [&](const QList<Lsp::Location>& locs) {
                resultStr = formatLocations(locs);
                loop.quit();
            });
        } else if (operation == "findReferences") {
            client->requestReferences(filePath, line, character, [&](const QList<Lsp::Location>& locs) {
                resultStr = formatLocations(locs);
                loop.quit();
            });
        } else if (operation == "hover") {
            client->requestHover(filePath, line, character, [&](const Lsp::Hover& hover) {
                resultStr = hover.contents.isEmpty() ? "无悬停信息" : hover.contents;
                loop.quit();
            });
        } else if (operation == "documentSymbol") {
            client->requestDocumentSymbols(filePath, [&](const QList<Lsp::DocumentSymbol>& symbols) {
                resultStr = "文档符号:\n";
                for (const auto& s : symbols)
                    resultStr += QString("- %1 (%2)\n").arg(s.name).arg(s.detail);
                loop.quit();
            });
        } else if (operation == "workspaceSymbol") {
            QString query = input["query"].toString();
            client->requestWorkspaceSymbols(query, [&](const QList<Lsp::SymbolInformation>& syms) {
                resultStr = "工作区符号:\n";
                for (const auto& s : syms)
                    resultStr += QString("- %1 (%2)\n").arg(s.name).arg(s.location.uri);
                loop.quit();
            });
        } else if (operation == "goToImplementation") {
            client->requestImplementation(filePath, line, character, [&](const QList<Lsp::Location>& locs) {
                resultStr = formatLocations(locs);
                loop.quit();
            });
        } else if (operation == "incomingCalls") {
            // 两步走：先 prepare 再查询 calls
            client->requestPrepareCallHierarchy(filePath, line, character, [&](const QList<Lsp::CallHierarchyItem>& items) {
                if (items.isEmpty()) {
                    resultStr = "未找到调用项";
                    loop.quit();
                    return;
                }
                client->requestIncomingCalls(items[0], [&](const QList<Lsp::CallHierarchyIncomingCall>& calls) {
                    resultStr = "被以下函数调用:\n";
                    for (const auto& c : calls)
                        resultStr += QString("- %1 (%2)\n").arg(c.from.name).arg(c.from.uri);
                    loop.quit();
                });
            });
        } else if (operation == "outgoingCalls") {
            client->requestPrepareCallHierarchy(filePath, line, character, [&](const QList<Lsp::CallHierarchyItem>& items) {
                if (items.isEmpty()) {
                    resultStr = "未找到调用项";
                    loop.quit();
                    return;
                }
                client->requestOutgoingCalls(items[0], [&](const QList<Lsp::CallHierarchyOutgoingCall>& calls) {
                    resultStr = "调用了以下函数:\n";
                    for (const auto& c : calls)
                        resultStr += QString("- %1 (%2)\n").arg(c.to.name).arg(c.to.uri);
                    loop.quit();
                });
            });
        } else {
            return "错误: 不支持的操作 " + operation;
        }

        loop.exec();
        if (resultStr.isEmpty() && timer.isActive())
            return "未找到结果";
        if (resultStr.isEmpty())
            return "错误: LSP 请求超时";
        return resultStr;
    }

    friend class LspToolTest;

private:
    static bool isCppFile(const QString& filePath)
    {
        QString ext = "." + QFileInfo(filePath).suffix().toLower();
        return ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".cc" || ext == ".cxx";
    }

    static void scheduleRetry(const QJsonObject& input, const QString& toolCallId)
    {
        if (toolCallId.isEmpty())
            return;
        static QSet<QString> scheduled;
        if (scheduled.contains(toolCallId))
            return;
        scheduled.insert(toolCallId);

        QJsonObject inputCopy = input;
        LspServerManager::instance()->addClangdWaiter([inputCopy, toolCallId](bool success, const QString&) {
            QString result;
            if (!success) {
                result = "错误: clangd 下载失败";
            } else {
                result = LspTool::execute(inputCopy);
                if (isDeferredToolResult(result)) {
                    result = "错误: clangd 仍未可用";
                }
            }
            scheduled.remove(toolCallId);
            AgentEventBus::instance()->postToolResult(toolCallId, result);
        });
    }

    static QString formatLocations(const QList<Lsp::Location>& locs)
    {
        if (locs.isEmpty())
            return "未找到结果";
        QString res = "找到结果:\n";
        for (const auto& l : locs) {
            res += QString("- %1:%2:%3\n")
                       .arg(Lsp::uriToPath(l.uri))
                       .arg(l.range.start.line + 1)
                       .arg(l.range.start.character + 1);
        }
        return res;
    }
};

#endif // LSPTOOL_H
