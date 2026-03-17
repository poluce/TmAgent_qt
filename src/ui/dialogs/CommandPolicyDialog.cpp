#include "CommandPolicyDialog.h"

#include "core/tools/ShellTool.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
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

QString arrayToEditorText(const QJsonArray& arr)
{
    QStringList lines;
    for (const QJsonValue& value : arr) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            lines.append(text);
    }
    lines.removeDuplicates();
    return lines.join(QLatin1Char('\n'));
}

QJsonArray editorTextToArray(const QString& text)
{
    QStringList lines;
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    for (const QString& line : rawLines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        lines.append(trimmed);
    }
    lines.removeDuplicates();

    QJsonArray arr;
    for (const QString& line : lines)
        arr.append(line);
    return arr;
}

void loadShellPolicyEditors(const QJsonObject& src,
                            QCheckBox* allowOutsideCheck,
                            QCheckBox* confirmExecCheck,
                            QCheckBox* enforceSafeCheck,
                            QSpinBox* timeoutSpin,
                            QPlainTextEdit* safeEdit,
                            QPlainTextEdit* dangerEdit,
                            QPlainTextEdit* writeEdit)
{
    const QJsonObject policy = ShellTool::normalizePolicyObject(src);
    allowOutsideCheck->setChecked(policy.value(QLatin1String("allow_outside_workspace")).toBool(false));
    confirmExecCheck->setChecked(policy.value(QLatin1String("confirm_executable")).toBool(true));
    enforceSafeCheck->setChecked(policy.value(QLatin1String("enforce_safe_prefixes")).toBool(false));
    timeoutSpin->setValue(qBound(1000, policy.value(QLatin1String("command_timeout_ms")).toInt(30000), 300000));
    safeEdit->setPlainText(arrayToEditorText(policy.value(QLatin1String("safe_command_prefixes")).toArray()));
    dangerEdit->setPlainText(arrayToEditorText(policy.value(QLatin1String("dangerous_patterns")).toArray()));
    writeEdit->setPlainText(arrayToEditorText(policy.value(QLatin1String("write_command_prefixes")).toArray()));
}

void loadToolLoopEditors(const QJsonObject& src,
                         const IGovernanceQueries* governanceQueries,
                         QSpinBox* maxToolRoundsSpin,
                         QSpinBox* maxSameToolRoundsSpin,
                         QSpinBox* maxNoProgressRoundsSpin,
                         QSpinBox* maxFailedRoundsSpin,
                         QSpinBox* maxTotalToolCallsSpin,
                         QSpinBox* maxWebFetchCallsSpin,
                         QSpinBox* maxToolLoopTimeSpin)
{
    if (!governanceQueries)
        return;
    const QJsonObject policy = governanceQueries->normalizeToolLoopPolicyObject(src);
    maxToolRoundsSpin->setValue(policy.value(QStringLiteral("max_tool_rounds_per_turn")).toInt(12));
    maxSameToolRoundsSpin->setValue(policy.value(QStringLiteral("max_consecutive_same_tool_rounds")).toInt(4));
    maxNoProgressRoundsSpin->setValue(policy.value(QStringLiteral("max_consecutive_no_progress_rounds")).toInt(4));
    maxFailedRoundsSpin->setValue(policy.value(QStringLiteral("max_consecutive_failed_tool_rounds")).toInt(3));
    maxTotalToolCallsSpin->setValue(policy.value(QStringLiteral("max_total_tool_calls_per_turn")).toInt(24));
    maxWebFetchCallsSpin->setValue(policy.value(QStringLiteral("max_web_fetch_calls_per_turn")).toInt(8));
    maxToolLoopTimeSpin->setValue(static_cast<int>(policy.value(QStringLiteral("max_tool_loop_time_ms")).toVariant().toLongLong()));
}

} // namespace

namespace CommandPolicyDialog {

void show(QWidget* parent,
          IGovernanceCommands* governanceCommands,
          const IGovernanceQueries* governanceQueries)
{
    if (!governanceCommands || !governanceQueries) {
        QMessageBox::warning(parent, QObject::tr("配置不可用"), QObject::tr("配置服务尚未初始化。"));
        return;
    }

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("命令权限设置"));
    dlg.setMinimumSize(920, 820);
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

    auto* title = new QLabel(QObject::tr("命令权限与工具循环配置"), titleContainer);
    title->setProperty("class", "SettingsTitle");
    auto* subTitle = new QLabel(QObject::tr("配置命令执行安全策略、黑白名单以及工具循环预算"), titleContainer);
    subTitle->setProperty("class", "SettingsSubTitle");

    titleVBox->addWidget(title);
    titleVBox->addWidget(subTitle);
    headerLayout->addWidget(titleContainer);
    headerLayout->addStretch();
    mainLayout->addWidget(header);

    auto* tabs = new QTabWidget(&dlg);
    tabs->setObjectName("SettingsTabWidget");
    if (auto* bar = tabs->tabBar())
        bar->setObjectName("SettingsTabBar");
    mainLayout->addWidget(tabs, 1);

    auto* cmdPage = new QWidget();
    auto* cmdLayout = new QVBoxLayout(cmdPage);
    cmdLayout->setContentsMargins(20, 20, 20, 20);
    cmdLayout->setSpacing(15);

    auto* optionsGroup = new QGroupBox(QObject::tr("安全选项"), cmdPage);
    optionsGroup->setProperty("class", "SettingsGroup");
    auto* optionsForm = new QFormLayout(optionsGroup);
    optionsForm->setContentsMargins(15, 20, 15, 15);
    optionsForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    optionsForm->setHorizontalSpacing(15);
    optionsForm->setVerticalSpacing(12);

    auto* allowOutsideCheck = new QCheckBox(QObject::tr("允许写命令跨工作空间（高风险）"), optionsGroup);
    auto* confirmExecCheck = new QCheckBox(QObject::tr("执行本地可执行文件前弹窗确认"), optionsGroup);
    auto* enforceSafeCheck = new QCheckBox(QObject::tr("启用白名单前缀校验（更严格）"), optionsGroup);
    auto* timeoutSpin = new QSpinBox(optionsGroup);
    timeoutSpin->setRange(1000, 300000);
    timeoutSpin->setSingleStep(1000);
    timeoutSpin->setSuffix(QStringLiteral(" ms"));

    optionsForm->addRow(QObject::tr("写入范围:"), allowOutsideCheck);
    optionsForm->addRow(QObject::tr("执行确认:"), confirmExecCheck);
    optionsForm->addRow(QObject::tr("命令准入:"), enforceSafeCheck);
    optionsForm->addRow(QObject::tr("命令超时:"), timeoutSpin);
    cmdLayout->addWidget(optionsGroup);

    auto* safeGroup = new QGroupBox(QObject::tr("白名单前缀"), cmdPage);
    safeGroup->setProperty("class", "SettingsGroup");
    auto* safeGroupLayout = new QVBoxLayout(safeGroup);
    safeGroupLayout->setContentsMargins(15, 20, 15, 15);
    auto* safeEdit = new QPlainTextEdit(safeGroup);
    safeEdit->setPlaceholderText(QObject::tr("每行一个，允许执行的命令前缀，例如：git clone"));
    safeEdit->setMinimumHeight(120);
    safeGroupLayout->addWidget(safeEdit);
    cmdLayout->addWidget(safeGroup);

    auto* dangerGroup = new QGroupBox(QObject::tr("黑名单模式"), cmdPage);
    dangerGroup->setProperty("class", "SettingsGroup");
    auto* dangerGroupLayout = new QVBoxLayout(dangerGroup);
    dangerGroupLayout->setContentsMargins(15, 20, 15, 15);
    auto* dangerEdit = new QPlainTextEdit(dangerGroup);
    dangerEdit->setPlaceholderText(QObject::tr("每行一个，命中即拒绝的模式，例如：rm -rf"));
    dangerEdit->setMinimumHeight(100);
    dangerGroupLayout->addWidget(dangerEdit);
    cmdLayout->addWidget(dangerGroup);

    auto* writeGroup = new QGroupBox(QObject::tr("写命令前缀"), cmdPage);
    writeGroup->setProperty("class", "SettingsGroup");
    auto* writeGroupLayout = new QVBoxLayout(writeGroup);
    writeGroupLayout->setContentsMargins(15, 20, 15, 15);
    auto* writeEdit = new QPlainTextEdit(writeGroup);
    writeEdit->setPlaceholderText(QObject::tr("每行一个，用于写入范围限制，例如：git clone"));
    writeEdit->setMinimumHeight(100);
    writeGroupLayout->addWidget(writeEdit);
    cmdLayout->addWidget(writeGroup);
    cmdLayout->addStretch();

    auto* cmdScroll = new QScrollArea(&dlg);
    cmdScroll->setWidgetResizable(true);
    cmdScroll->setFrameShape(QFrame::NoFrame);
    cmdScroll->setWidget(cmdPage);
    tabs->addTab(cmdScroll, QObject::tr("命令权限"));

    auto* toolLoopPage = new QWidget();
    auto* toolLoopPageLayout = new QVBoxLayout(toolLoopPage);
    toolLoopPageLayout->setContentsMargins(20, 20, 20, 20);
    toolLoopPageLayout->setSpacing(15);

    auto* toolLoopGroup = new QGroupBox(QObject::tr("循环预算"), toolLoopPage);
    toolLoopGroup->setProperty("class", "SettingsGroup");
    auto* toolLoopForm = new QFormLayout(toolLoopGroup);
    toolLoopForm->setContentsMargins(15, 20, 15, 15);
    toolLoopForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    toolLoopForm->setHorizontalSpacing(15);
    toolLoopForm->setVerticalSpacing(12);

    auto* maxToolRoundsSpin = new QSpinBox(toolLoopGroup);
    maxToolRoundsSpin->setRange(2, 64);
    auto* maxSameToolRoundsSpin = new QSpinBox(toolLoopGroup);
    maxSameToolRoundsSpin->setRange(1, 32);
    auto* maxNoProgressRoundsSpin = new QSpinBox(toolLoopGroup);
    maxNoProgressRoundsSpin->setRange(1, 32);
    auto* maxFailedRoundsSpin = new QSpinBox(toolLoopGroup);
    maxFailedRoundsSpin->setRange(1, 32);
    auto* maxTotalToolCallsSpin = new QSpinBox(toolLoopGroup);
    maxTotalToolCallsSpin->setRange(4, 256);
    auto* maxWebFetchCallsSpin = new QSpinBox(toolLoopGroup);
    maxWebFetchCallsSpin->setRange(1, 128);
    auto* maxToolLoopTimeSpin = new QSpinBox(toolLoopGroup);
    maxToolLoopTimeSpin->setRange(5000, 300000);
    maxToolLoopTimeSpin->setSingleStep(5000);
    maxToolLoopTimeSpin->setSuffix(QStringLiteral(" ms"));

    toolLoopForm->addRow(QObject::tr("单回合最大轮次:"), maxToolRoundsSpin);
    toolLoopForm->addRow(QObject::tr("同参数重复上限:"), maxSameToolRoundsSpin);
    toolLoopForm->addRow(QObject::tr("无进展轮次上限:"), maxNoProgressRoundsSpin);
    toolLoopForm->addRow(QObject::tr("连续失败轮次上限:"), maxFailedRoundsSpin);
    toolLoopForm->addRow(QObject::tr("单回合工具调用总数上限:"), maxTotalToolCallsSpin);
    toolLoopForm->addRow(QObject::tr("单回合 web_fetch 上限:"), maxWebFetchCallsSpin);
    toolLoopForm->addRow(QObject::tr("单回合总时长上限:"), maxToolLoopTimeSpin);
    toolLoopPageLayout->addWidget(toolLoopGroup);
    toolLoopPageLayout->addStretch();

    auto* toolLoopScroll = new QScrollArea(&dlg);
    toolLoopScroll->setWidgetResizable(true);
    toolLoopScroll->setFrameShape(QFrame::NoFrame);
    toolLoopScroll->setWidget(toolLoopPage);
    tabs->addTab(toolLoopScroll, QObject::tr("工具循环"));

    loadShellPolicyEditors(ShellTool::loadPolicyObject(),
                           allowOutsideCheck,
                           confirmExecCheck,
                           enforceSafeCheck,
                           timeoutSpin,
                           safeEdit,
                           dangerEdit,
                           writeEdit);
    loadToolLoopEditors(governanceQueries->loadToolLoopPolicyObject(),
                        governanceQueries,
                        maxToolRoundsSpin,
                        maxSameToolRoundsSpin,
                        maxNoProgressRoundsSpin,
                        maxFailedRoundsSpin,
                        maxTotalToolCallsSpin,
                        maxWebFetchCallsSpin,
                        maxToolLoopTimeSpin);

    auto* footer = new QFrame(&dlg);
    footer->setFixedHeight(60);
    footer->setProperty("class", "SettingsFooter");
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(20, 0, 20, 0);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, footer);
    if (QPushButton* saveBtn = buttons->button(QDialogButtonBox::Save))
        saveBtn->setText(QObject::tr("保存"));
    if (QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel))
        cancelBtn->setText(QObject::tr("取消"));
    QPushButton* resetBtn = buttons->addButton(QObject::tr("恢复默认"), QDialogButtonBox::ResetRole);
    footerLayout->addStretch();
    footerLayout->addWidget(buttons);
    mainLayout->addWidget(footer);

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(resetBtn, &QPushButton::clicked, &dlg, [=]() {
        loadShellPolicyEditors(ShellTool::defaultPolicyObject(),
                               allowOutsideCheck,
                               confirmExecCheck,
                               enforceSafeCheck,
                               timeoutSpin,
                               safeEdit,
                               dangerEdit,
                               writeEdit);
        loadToolLoopEditors(governanceQueries->defaultToolLoopPolicyObject(),
                            governanceQueries,
                            maxToolRoundsSpin,
                            maxSameToolRoundsSpin,
                            maxNoProgressRoundsSpin,
                            maxFailedRoundsSpin,
                            maxTotalToolCallsSpin,
                            maxWebFetchCallsSpin,
                            maxToolLoopTimeSpin);
    });

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [=, &dlg]() {
        QJsonObject raw;
        raw.insert(QStringLiteral("allow_outside_workspace"), allowOutsideCheck->isChecked());
        raw.insert(QStringLiteral("confirm_executable"), confirmExecCheck->isChecked());
        raw.insert(QStringLiteral("enforce_safe_prefixes"), enforceSafeCheck->isChecked());
        raw.insert(QStringLiteral("command_timeout_ms"), timeoutSpin->value());
        raw.insert(QStringLiteral("safe_command_prefixes"), editorTextToArray(safeEdit->toPlainText()));
        raw.insert(QStringLiteral("dangerous_patterns"), editorTextToArray(dangerEdit->toPlainText()));
        raw.insert(QStringLiteral("write_command_prefixes"), editorTextToArray(writeEdit->toPlainText()));

        QString err;
        if (!ShellTool::savePolicyObject(raw, &err)) {
            QMessageBox::warning(parent,
                                 QObject::tr("保存失败"),
                                 err.isEmpty() ? QObject::tr("无法写入命令权限配置。") : err);
            return;
        }

        QJsonObject toolLoopRaw;
        toolLoopRaw.insert(QStringLiteral("schema_version"), 4);
        toolLoopRaw.insert(QStringLiteral("max_tool_rounds_per_turn"), maxToolRoundsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_consecutive_same_tool_rounds"), maxSameToolRoundsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_consecutive_no_progress_rounds"), maxNoProgressRoundsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_consecutive_failed_tool_rounds"), maxFailedRoundsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_total_tool_calls_per_turn"), maxTotalToolCallsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_web_fetch_calls_per_turn"), maxWebFetchCallsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_tool_loop_time_ms"), maxToolLoopTimeSpin->value());

        QString toolLoopErr;
        if (!governanceCommands->saveToolLoopPolicyObject(toolLoopRaw, &toolLoopErr)) {
            QMessageBox::warning(parent,
                                 QObject::tr("保存失败"),
                                 toolLoopErr.isEmpty() ? QObject::tr("无法写入工具循环配置。") : toolLoopErr);
            return;
        }

        QMessageBox::information(parent,
                                 QObject::tr("保存成功"),
                                 QObject::tr("命令权限与工具循环配置已更新，将在下一次工具调用时生效。"));
        dlg.accept();
    });

    dlg.exec();
}

} // namespace CommandPolicyDialog
