#ifndef LSPINSTALLTOOL_H
#define LSPINSTALLTOOL_H

#include <QJsonObject>
#include <QString>

/**
 * @brief LSP 安装/下载工具（目前仅支持 clangd）
 */
class LspInstallTool {
public:
    static QString execute(const QJsonObject& input);

private:
    static void scheduleInstall(const QString& toolCallId);
};

#endif // LSPINSTALLTOOL_H
