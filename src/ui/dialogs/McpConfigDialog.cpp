#include "McpConfigDialog.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

void applyAppStyleSheet(QDialog& dlg)
{
    QFile qssFile(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/styles/app.qss")));
    if (qssFile.open(QFile::ReadOnly)) {
        dlg.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
        qssFile.close();
    }
}

} // namespace

namespace McpConfigDialog {

void show(QWidget* parent, IAppFacade& app)
{
    auto* governance = &app.governance();
    if (!governance)
        return;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("配置 MCP 工具服务"));
    dlg.setMinimumSize(700, 600);
    applyAppStyleSheet(dlg);

    auto* mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* header = new QFrame(&dlg);
    header->setFixedHeight(60);
    header->setProperty("class", "SettingsHeader");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 0, 20, 0);

    auto* titleContainer = new QWidget(header);
    auto* titleVBox = new QVBoxLayout(titleContainer);
    titleVBox->setContentsMargins(0, 0, 0, 0);
    titleVBox->setSpacing(2);

    auto* title = new QLabel(QObject::tr("MCP 工具服务配置"), titleContainer);
    title->setProperty("class", "SettingsTitle");
    auto* subTitle = new QLabel(QObject::tr("管理外部 MCP 服务器连接，每行一条配置"), titleContainer);
    subTitle->setProperty("class", "SettingsSubTitle");

    titleVBox->addWidget(title);
    titleVBox->addWidget(subTitle);
    headerLayout->addWidget(titleContainer);
    headerLayout->addStretch();
    mainLayout->addWidget(header);

    auto* contentPage = new QWidget(&dlg);
    auto* contentLayout = new QVBoxLayout(contentPage);
    contentLayout->setContentsMargins(20, 20, 20, 20);
    contentLayout->setSpacing(15);

    auto* serverGroup = new QGroupBox(QObject::tr("服务器列表"), contentPage);
    serverGroup->setProperty("class", "SettingsGroup");
    auto* serverGroupLayout = new QVBoxLayout(serverGroup);
    serverGroupLayout->setContentsMargins(15, 20, 15, 15);
    serverGroupLayout->setSpacing(10);

    auto* hint = new QLabel(QObject::tr("格式：name|url|token|header|prefix|async\n"
                                        "示例: exa|https://example.com/mcp|TOKEN|Authorization|1|1\n"
                                        "说明: prefix=1 将工具名前缀为 name:tool，async=1 使用异步回传。"),
                             serverGroup);
    hint->setWordWrap(true);
    hint->setProperty("class", "SettingsSubTitle");
    serverGroupLayout->addWidget(hint);

    auto* editor = new QPlainTextEdit(serverGroup);
    editor->setPlainText(governance->loadMcpConfigSpecs().join('\n'));
    serverGroupLayout->addWidget(editor, 1);

    auto* envHint = new QLabel(QObject::tr("注意：环境变量 TMAGENT_MCP_SERVERS 会在运行时追加，但不会写入此配置。"),
                                serverGroup);
    envHint->setWordWrap(true);
    envHint->setProperty("class", "SettingsSubTitle");
    serverGroupLayout->addWidget(envHint);
    contentLayout->addWidget(serverGroup);

    auto* contentScroll = new QScrollArea(&dlg);
    contentScroll->setWidgetResizable(true);
    contentScroll->setFrameShape(QFrame::NoFrame);
    contentScroll->setWidget(contentPage);
    mainLayout->addWidget(contentScroll, 1);

    auto* footer = new QFrame(&dlg);
    footer->setFixedHeight(60);
    footer->setProperty("class", "SettingsFooter");
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(20, 0, 20, 0);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, footer);
    if (QPushButton* okBtn = buttons->button(QDialogButtonBox::Ok))
        okBtn->setText(QObject::tr("确定"));
    if (QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel))
        cancelBtn->setText(QObject::tr("取消"));
    footerLayout->addStretch();
    footerLayout->addWidget(buttons);
    mainLayout->addWidget(footer);

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [=, &dlg]() {
        QStringList newSpecs;
        const QStringList lines = editor->toPlainText().split('\n');
        for (const QString& line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
                continue;
            newSpecs.append(trimmed);
        }

        if (!governance->saveMcpConfigSpecs(newSpecs)) {
            QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("无法写入 MCP 配置文件。"));
            return;
        }

        governance->applyMcpConfig(newSpecs);
        governance->applyToolDispatcherToAllRuntimes();
        QMessageBox::information(parent, QObject::tr("配置已保存"), QObject::tr("MCP 配置已更新。"));
        dlg.accept();
    });

    dlg.exec();
}

} // namespace McpConfigDialog
