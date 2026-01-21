#ifndef LSPTOOL_H
#define LSPTOOL_H

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include "core/lsp/LspServerManager.h"
#include "core/lsp/LspClient.h"
#include "core/lsp/LspProtocol.h"

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
    static QString execute(const QJsonObject& input) {
        QString operation = input["operation"].toString();
        QString filePath = input["file_path"].toString();
        // Agent 传进来的是 0-based，符合底层逻辑
        int line = input["line"].toInt();
        int character = input["character"].toInt();

        LspClient* client = LspServerManager::instance()->getClientForFile(filePath);
        if (!client) return "错误: 无法为该文件启动 LSP 服务器";

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
                for (const auto& s : symbols) resultStr += QString("- %1 (%2)\n").arg(s.name).arg(s.detail);
                loop.quit();
            });
        } else if (operation == "workspaceSymbol") {
            QString query = input["query"].toString();
            client->requestWorkspaceSymbols(query, [&](const QList<Lsp::SymbolInformation>& syms) {
                resultStr = "工作区符号:\n";
                for (const auto& s : syms) resultStr += QString("- %1 (%2)\n").arg(s.name).arg(s.location.uri);
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
                if (items.isEmpty()) { resultStr = "未找到调用项"; loop.quit(); return; }
                client->requestIncomingCalls(items[0], [&](const QList<Lsp::CallHierarchyIncomingCall>& calls) {
                    resultStr = "被以下函数调用:\n";
                    for (const auto& c : calls) resultStr += QString("- %1 (%2)\n").arg(c.from.name).arg(c.from.uri);
                    loop.quit();
                });
            });
        } else if (operation == "outgoingCalls") {
            client->requestPrepareCallHierarchy(filePath, line, character, [&](const QList<Lsp::CallHierarchyItem>& items) {
                if (items.isEmpty()) { resultStr = "未找到调用项"; loop.quit(); return; }
                client->requestOutgoingCalls(items[0], [&](const QList<Lsp::CallHierarchyOutgoingCall>& calls) {
                    resultStr = "调用了以下函数:\n";
                    for (const auto& c : calls) resultStr += QString("- %1 (%2)\n").arg(c.to.name).arg(c.to.uri);
                    loop.quit();
                });
            });
        } else {
            return "错误: 不支持的操作 " + operation;
        }

        loop.exec();
        if (resultStr.isEmpty() && timer.isActive()) return "未找到结果";
        if (resultStr.isEmpty()) return "错误: LSP 请求超时";
        return resultStr;
    }

private:
    static QString formatLocations(const QList<Lsp::Location>& locs) {
        if (locs.isEmpty()) return "未找到结果";
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
