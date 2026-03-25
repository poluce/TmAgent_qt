#include "InformationSettingsDialog.h"
#include "ToolPermissionEditor.h"

#include "core/manager/IdentityManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/tools/AgentToolNames.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {

void applyAppStyleSheet(QDialog& dlg)
{
    QFile qssFile(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/styles/app.qss")));
    if (qssFile.open(QFile::ReadOnly)) {
        dlg.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
        qssFile.close();
    }
}

QHash<QString, QString> parseUserProfileFields(const QString& markdown)
{
    QHash<QString, QString> fields;
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    static const QRegularExpression lineRe(
        QStringLiteral("^\\s*-\\s*([A-Za-z0-9_]+)\\s*:\\s*(.*)\\s*$"));
    for (const QString& line : lines) {
        const QRegularExpressionMatch m = lineRe.match(line);
        if (!m.hasMatch())
            continue;
        const QString key = m.captured(1).trimmed();
        const QString value = m.captured(2).trimmed();
        if (!key.isEmpty())
            fields.insert(key, value);
    }
    return fields;
}

QString parseUserNotesSection(const QString& markdown)
{
    const QString marker = QStringLiteral("## User Notes");
    const int pos = markdown.indexOf(marker);
    if (pos < 0)
        return QString();
    QString notes = markdown.mid(pos + marker.size()).trimmed();
    if (notes.startsWith(QLatin1Char('\n')))
        notes.remove(0, 1);
    return notes.trimmed();
}

QString buildUserProfileMarkdown(const QHash<QString, QString>& fields, const QString& notes)
{
    auto val = [&fields](const QString& key) {
        return fields.value(key).trimmed();
    };

    QString text;
    text += QStringLiteral("# User Profile\n\n");
    text += QStringLiteral("- preferred_name: %1\n").arg(val(QStringLiteral("preferred_name")));
    text += QStringLiteral("- self_identity: %1\n").arg(val(QStringLiteral("self_identity")));
    text += QStringLiteral("- focus_goals: %1\n").arg(val(QStringLiteral("focus_goals")));
    text += QStringLiteral("- preference_traits: %1\n").arg(val(QStringLiteral("preference_traits")));
    text += QStringLiteral("- company_culture: %1\n").arg(val(QStringLiteral("company_culture")));
    text += QStringLiteral("- communication_style: %1\n").arg(val(QStringLiteral("communication_style")));
    text += QStringLiteral("- long_term_preferences: %1\n").arg(val(QStringLiteral("long_term_preferences")));
    text += QStringLiteral("\n## User Notes\n\n");
    text += notes.trimmed();
    text += QLatin1Char('\n');
    return text;
}

QDateTime parseIsoDateTimeToUtc(const QString& raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return QDateTime();
    QDateTime dt = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(text, Qt::ISODate);
    if (!dt.isValid())
        return QDateTime();
    return dt.toUTC();
}

QString utcFieldToLocalText(const QJsonObject& obj, const QString& key)
{
    const QDateTime utc = parseIsoDateTimeToUtc(obj.value(key).toString());
    if (!utc.isValid())
        return QStringLiteral("—");
    return utc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace

namespace InformationSettingsDialog {

void show(QWidget* parent, IAppFacade& app, const QString& activeIdentityId)
{
    auto* workspace = &app.workspace();
    auto* memory = &app.memory();
    auto* governance = &app.governance();
    if (!workspace || !memory || !governance)
        return;

    const bool canManageGlobalConfig =
        workspace->canIdentityManageGlobalConfig(activeIdentityId);
    Identity* activeIdentity = IdentityManager::instance()->findById(activeIdentityId);
    const bool isAgentIdentity = activeIdentity && activeIdentity->isAgent();

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("信息设置"));
    dlg.setMinimumSize(900, 820);
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

    auto* title = new QLabel(QObject::tr("智能助手信息配置"), titleContainer);
    title->setProperty("class", "SettingsTitle");
    auto* subTitle = new QLabel(QObject::tr("配置记忆管家、用户画像以及自动化巡检（心跳）任务"), titleContainer);
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

    auto createScrollPage = [&](QWidget* page) {
        auto* sa = new QScrollArea(&dlg);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setWidget(page);
        return sa;
    };

    auto* memoryPage = new QWidget();
    auto* memoryLayout = new QVBoxLayout(memoryPage);
    memoryLayout->setContentsMargins(20, 20, 20, 20);
    memoryLayout->setSpacing(15);

    auto* memoryGroup = new QGroupBox(QObject::tr("记忆管家与策略"), memoryPage);
    memoryGroup->setProperty("class", "SettingsGroup");
    auto* memoryForm = new QFormLayout(memoryGroup);
    memoryForm->setContentsMargins(15, 20, 15, 15);
    memoryForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    memoryForm->setHorizontalSpacing(15);
    memoryForm->setVerticalSpacing(12);

    auto* memoryStewardCombo = new QComboBox(memoryGroup);
    memoryStewardCombo->setMinimumWidth(300);
    memoryForm->addRow(QObject::tr("记忆管家助手:"), memoryStewardCombo);

    auto* memoryStewardModelCombo = new QComboBox(memoryGroup);
    memoryStewardModelCombo->setMinimumWidth(300);
    memoryForm->addRow(QObject::tr("管家使用模型:"), memoryStewardModelCombo);

    auto* memoryAutoExtractCheck = new QCheckBox(QObject::tr("开启自动提炼长期记忆"), memoryGroup);
    memoryForm->addRow(QObject::tr("自动化策略:"), memoryAutoExtractCheck);

    auto* memoryMinCharsSpin = new QSpinBox(memoryGroup);
    memoryMinCharsSpin->setRange(1, 4096);
    memoryMinCharsSpin->setSuffix(QObject::tr(" 字"));
    memoryForm->addRow(QObject::tr("触发提炼最小长度:"), memoryMinCharsSpin);

    auto* memoryMaxCandidatesSpin = new QSpinBox(memoryGroup);
    memoryMaxCandidatesSpin->setRange(1, 32);
    memoryForm->addRow(QObject::tr("每回合提炼上限:"), memoryMaxCandidatesSpin);

    auto* memoryReflectCheck = new QCheckBox(QObject::tr("开启定期反思与质量评分"), memoryGroup);
    memoryForm->addRow(QObject::tr("反思任务:"), memoryReflectCheck);

    auto* memoryReflectEveryTurnsSpin = new QSpinBox(memoryGroup);
    memoryReflectEveryTurnsSpin->setRange(1, 200);
    memoryReflectEveryTurnsSpin->setSuffix(QObject::tr(" 回合"));
    memoryForm->addRow(QObject::tr("反思触发间隔:"), memoryReflectEveryTurnsSpin);

    auto* memoryReflectMaxCandidatesSpin = new QSpinBox(memoryGroup);
    memoryReflectMaxCandidatesSpin->setRange(1, 32);
    memoryForm->addRow(QObject::tr("单次反思候选上限:"), memoryReflectMaxCandidatesSpin);

    auto* memoryReflectScanDailyFilesSpin = new QSpinBox(memoryGroup);
    memoryReflectScanDailyFilesSpin->setRange(1, 30);
    memoryReflectScanDailyFilesSpin->setSuffix(QObject::tr(" 天"));
    memoryForm->addRow(QObject::tr("反思扫描近日报数:"), memoryReflectScanDailyFilesSpin);

    auto* memoryActionRow = new QHBoxLayout();
    auto* memoryReindexBtn = new QPushButton(QObject::tr("重建记忆索引"), memoryGroup);
    memoryReindexBtn->setFixedWidth(140);
    memoryActionRow->addStretch();
    memoryActionRow->addWidget(memoryReindexBtn);
    memoryForm->addRow(QObject::tr("维护操作:"), memoryActionRow);

    memoryLayout->addWidget(memoryGroup);
    memoryLayout->addStretch();
    tabs->addTab(createScrollPage(memoryPage), QObject::tr("记忆系统"));

    auto* userPage = new QWidget();
    auto* userLayout = new QVBoxLayout(userPage);
    userLayout->setContentsMargins(20, 20, 20, 20);
    userLayout->setSpacing(15);

    auto* userGroup = new QGroupBox(QObject::tr("核心画像与背景"), userPage);
    userGroup->setProperty("class", "SettingsGroup");
    auto* userForm = new QFormLayout(userGroup);
    userForm->setContentsMargins(15, 20, 15, 15);
    userForm->setHorizontalSpacing(15);
    userForm->setVerticalSpacing(12);

    auto* userPreferredNameEdit = new QLineEdit(userGroup);
    userForm->addRow(QObject::tr("用户称呼:"), userPreferredNameEdit);

    auto* userIdentityEdit = new QLineEdit(userGroup);
    userForm->addRow(QObject::tr("身份定位:"), userIdentityEdit);

    auto* userGoalsEdit = new QPlainTextEdit(userGroup);
    userGoalsEdit->setMinimumHeight(100);
    userForm->addRow(QObject::tr("长期目标:"), userGoalsEdit);

    auto* userPreferencesEdit = new QPlainTextEdit(userGroup);
    userPreferencesEdit->setMinimumHeight(100);
    userForm->addRow(QObject::tr("偏好倾向:"), userPreferencesEdit);

    auto* companyCultureEdit = new QPlainTextEdit(userGroup);
    companyCultureEdit->setMinimumHeight(100);
    userForm->addRow(QObject::tr("企业文化:"), companyCultureEdit);

    auto* userNotesEdit = new QPlainTextEdit(userGroup);
    userNotesEdit->setMinimumHeight(120);
    userForm->addRow(QObject::tr("补充备注:"), userNotesEdit);

    userLayout->addWidget(userGroup);
    userLayout->addStretch();
    tabs->addTab(createScrollPage(userPage), QObject::tr("用户信息"));

    ToolPermissionEditor* agentToolEditor = nullptr;
    QCheckBox* agentDelegateCheck = nullptr;
    if (isAgentIdentity) {
        auto* agentToolsPage = new QWidget();
        auto* agentToolsLayout = new QVBoxLayout(agentToolsPage);
        agentToolsLayout->setContentsMargins(20, 20, 20, 20);
        agentToolsLayout->setSpacing(15);

        auto* agentToolsGroup = new QGroupBox(QObject::tr("Agent 工具权限"), agentToolsPage);
        agentToolsGroup->setProperty("class", "SettingsGroup");
        auto* agentToolsGroupLayout = new QVBoxLayout(agentToolsGroup);
        agentToolsGroupLayout->setContentsMargins(15, 20, 15, 15);
        agentToolsGroupLayout->setSpacing(10);

        auto* agentToolsHint = new QLabel(
            QObject::tr("按插件来源管理当前 Agent 的可用工具。已保存但当前未加载的工具会保留显示。"),
            agentToolsGroup);
        agentToolsHint->setWordWrap(true);
        agentToolsGroupLayout->addWidget(agentToolsHint);

        agentDelegateCheck = new QCheckBox(QObject::tr("允许团队协作类工具"), agentToolsGroup);
        agentDelegateCheck->setToolTip(QObject::tr("关闭后会移除所有 teammate 团队协作相关工具。"));
        agentToolsGroupLayout->addWidget(agentDelegateCheck);

        agentToolEditor = new ToolPermissionEditor(agentToolsGroup);
        agentToolsGroupLayout->addWidget(agentToolEditor, 1);

        agentToolsLayout->addWidget(agentToolsGroup, 1);
        tabs->addTab(createScrollPage(agentToolsPage), QObject::tr("工具权限"));
    }

    auto* heartbeatPage = new QWidget();
    auto* heartbeatLayout = new QVBoxLayout(heartbeatPage);
    heartbeatLayout->setContentsMargins(20, 20, 20, 20);
    heartbeatLayout->setSpacing(15);

    auto* heartbeatGroup = new QGroupBox(QObject::tr("后台巡检策略"), heartbeatPage);
    heartbeatGroup->setProperty("class", "SettingsGroup");
    auto* heartbeatForm = new QFormLayout(heartbeatGroup);
    heartbeatForm->setContentsMargins(15, 20, 15, 15);
    heartbeatForm->setHorizontalSpacing(15);
    heartbeatForm->setVerticalSpacing(10);

    auto* heartbeatAgentCombo = new QComboBox(heartbeatGroup);
    heartbeatAgentCombo->setMinimumWidth(300);
    heartbeatForm->addRow(QObject::tr("执行助手:"), heartbeatAgentCombo);

    auto* heartbeatEnabledCheck = new QCheckBox(QObject::tr("启用自动心跳"), heartbeatGroup);
    heartbeatForm->addRow(QObject::tr("状态开关:"), heartbeatEnabledCheck);

    auto* heartbeatIntervalSpin = new QSpinBox(heartbeatGroup);
    heartbeatIntervalSpin->setRange(5, 24 * 60 * 60);
    heartbeatIntervalSpin->setSuffix(QObject::tr(" 秒"));
    heartbeatForm->addRow(QObject::tr("巡检节奏:"), heartbeatIntervalSpin);

    auto* hbNotifyBox = new QWidget(heartbeatGroup);
    auto* hbNotifyLayout = new QVBoxLayout(hbNotifyBox);
    hbNotifyLayout->setContentsMargins(0, 0, 0, 0);
    auto* heartbeatSilentNoChangeCheck = new QCheckBox(QObject::tr("仅关键变化投递摘要"), hbNotifyBox);
    auto* heartbeatNotifyOnChangeOnlyCheck = new QCheckBox(QObject::tr("关键变化升级到 LLM"), hbNotifyBox);
    hbNotifyLayout->addWidget(heartbeatSilentNoChangeCheck);
    hbNotifyLayout->addWidget(heartbeatNotifyOnChangeOnlyCheck);
    heartbeatForm->addRow(QObject::tr("通知策略:"), hbNotifyBox);

    auto* heartbeatNotifyIntervalSpin = new QSpinBox(heartbeatGroup);
    heartbeatNotifyIntervalSpin->setRange(0, 300);
    heartbeatNotifyIntervalSpin->setSuffix(QObject::tr(" 秒"));
    heartbeatNotifyIntervalSpin->setValue(5);
    heartbeatNotifyIntervalSpin->setToolTip(
        QObject::tr("应用启动后的缓冲时间。若错过心跳窗口，会在该时间后尝试补跑 1 次。"));
    heartbeatForm->addRow(QObject::tr("启动缓冲:"), heartbeatNotifyIntervalSpin);

    auto* heartbeatPersistNoChangeCheck = new QCheckBox(QObject::tr("巡检时触发记忆维护"), heartbeatGroup);
    heartbeatPersistNoChangeCheck->setChecked(true);
    heartbeatPersistNoChangeCheck->setToolTip(
        QObject::tr("启用后，后台心跳会在规则巡检后触发记忆反思等维护动作。"));
    heartbeatForm->addRow(QObject::tr("维护策略:"), heartbeatPersistNoChangeCheck);

    auto* heartbeatStatePersistIntervalSpin = new QSpinBox(heartbeatGroup);
    heartbeatStatePersistIntervalSpin->setRange(10, 60000);
    heartbeatStatePersistIntervalSpin->setSuffix(QObject::tr(" ms"));
    heartbeatStatePersistIntervalSpin->setValue(250);
    heartbeatStatePersistIntervalSpin->setToolTip(QObject::tr("多次心跳请求在该窗口内合并为一张待处理票据。"));
    heartbeatForm->addRow(QObject::tr("合并窗口:"), heartbeatStatePersistIntervalSpin);

    auto* hbExtraBox = new QWidget(heartbeatGroup);
    auto* hbExtraForm = new QFormLayout(hbExtraBox);
    hbExtraForm->setContentsMargins(0, 0, 0, 0);
    auto* heartbeatStartEdit = new QLineEdit(hbExtraBox);
    auto* heartbeatEndEdit = new QLineEdit(hbExtraBox);
    auto* heartbeatTimezoneEdit = new QLineEdit(hbExtraBox);
    hbExtraForm->addRow(QObject::tr("活跃开始:"), heartbeatStartEdit);
    hbExtraForm->addRow(QObject::tr("活跃结束:"), heartbeatEndEdit);
    hbExtraForm->addRow(QObject::tr("时区:"), heartbeatTimezoneEdit);
    heartbeatForm->addRow(QObject::tr("时间段限制:"), hbExtraBox);

    auto* heartbeatSignalsRow = new QWidget(heartbeatGroup);
    auto* heartbeatSignalsLayout = new QHBoxLayout(heartbeatSignalsRow);
    heartbeatSignalsLayout->setContentsMargins(0, 0, 0, 0);
    heartbeatSignalsLayout->setSpacing(8);
    auto* heartbeatSignalProviderCheck = new QCheckBox(QObject::tr("Provider"), heartbeatSignalsRow);
    auto* heartbeatSignalDelegateCheck = new QCheckBox(QObject::tr("团队协作"), heartbeatSignalsRow);
    auto* heartbeatSignalPulseCheck = new QCheckBox(QObject::tr("状态"), heartbeatSignalsRow);
    auto* heartbeatSignalSchedulerCheck = new QCheckBox(QObject::tr("定时"), heartbeatSignalsRow);
    auto* heartbeatSignalMemoryCheck = new QCheckBox(QObject::tr("记忆"), heartbeatSignalsRow);
    heartbeatSignalsLayout->addWidget(heartbeatSignalProviderCheck);
    heartbeatSignalsLayout->addWidget(heartbeatSignalDelegateCheck);
    heartbeatSignalsLayout->addWidget(heartbeatSignalPulseCheck);
    heartbeatSignalsLayout->addWidget(heartbeatSignalSchedulerCheck);
    heartbeatSignalsLayout->addWidget(heartbeatSignalMemoryCheck);
    heartbeatForm->addRow(QObject::tr("监视模块:"), heartbeatSignalsRow);

    auto* heartbeatSignalExtraEdit = new QLineEdit(heartbeatGroup);
    heartbeatSignalExtraEdit->setPlaceholderText(QObject::tr("未开放额外模块"));
    heartbeatSignalExtraEdit->setEnabled(false);
    heartbeatForm->addRow(QObject::tr("扩展模块:"), heartbeatSignalExtraEdit);

    auto* heartbeatPathLabel = new QLabel(heartbeatGroup);
    heartbeatPathLabel->setProperty("class", "PathLabel");
    heartbeatPathLabel->setWordWrap(true);
    heartbeatForm->addRow(QObject::tr("补充指令文件:"), heartbeatPathLabel);

    auto* heartbeatInstructionEdit = new QPlainTextEdit(heartbeatGroup);
    heartbeatInstructionEdit->setMinimumHeight(120);
    heartbeatForm->addRow(QObject::tr("后台升级补充指令:"), heartbeatInstructionEdit);

    auto* heartbeatActionRow = new QHBoxLayout();
    auto* heartbeatApplyBtn = new QPushButton(QObject::tr("保存巡检策略"), heartbeatGroup);
    auto* heartbeatTriggerBtn = new QPushButton(QObject::tr("手动巡检"), heartbeatGroup);
    heartbeatActionRow->addWidget(heartbeatApplyBtn);
    heartbeatActionRow->addWidget(heartbeatTriggerBtn);
    heartbeatActionRow->addStretch(1);
    heartbeatForm->addRow(QString(), heartbeatActionRow);

    heartbeatLayout->addWidget(heartbeatGroup);
    heartbeatLayout->addStretch();
    tabs->addTab(createScrollPage(heartbeatPage), QObject::tr("心跳设置"));

    auto* statePage = new QWidget();
    auto* stateLayout = new QVBoxLayout(statePage);
    stateLayout->setContentsMargins(20, 20, 20, 20);
    stateLayout->setSpacing(15);

    auto* stateGroup = new QGroupBox(QObject::tr("心跳运行状态"), statePage);
    stateGroup->setProperty("class", "SettingsGroup");
    auto* stateForm = new QFormLayout(stateGroup);
    stateForm->setContentsMargins(15, 20, 15, 15);
    stateForm->setVerticalSpacing(10);

    auto* heartbeatStatePathLabel = new QLabel(stateGroup);
    heartbeatStatePathLabel->setProperty("class", "PathLabel");
    stateForm->addRow(QObject::tr("运行时状态:"), heartbeatStatePathLabel);

    auto* heartbeatLastSnapshotLabel = new QLabel(QStringLiteral("—"), stateGroup);
    stateForm->addRow(QObject::tr("上次执行:"), heartbeatLastSnapshotLabel);
    auto* heartbeatLastNotifyLabel = new QLabel(QStringLiteral("—"), stateGroup);
    stateForm->addRow(QObject::tr("上次投递摘要:"), heartbeatLastNotifyLabel);
    auto* heartbeatLastChangeLabel = new QLabel(QStringLiteral("—"), stateGroup);
    stateForm->addRow(QObject::tr("下次计划时间:"), heartbeatLastChangeLabel);
    auto* heartbeatReasonLabel = new QLabel(QStringLiteral("—"), stateGroup);
    heartbeatReasonLabel->setWordWrap(true);
    stateForm->addRow(QObject::tr("最近延后原因:"), heartbeatReasonLabel);
    auto* heartbeatJobsLabel = new QLabel(QStringLiteral("0"), stateGroup);
    stateForm->addRow(QObject::tr("待处理票据:"), heartbeatJobsLabel);
    auto* heartbeatProviderDownLabel = new QLabel(QStringLiteral("unknown"), stateGroup);
    stateForm->addRow(QObject::tr("Provider 状态:"), heartbeatProviderDownLabel);
    auto* heartbeatWatchSignalsLabel = new QLabel(QStringLiteral("—"), stateGroup);
    heartbeatWatchSignalsLabel->setWordWrap(true);
    stateForm->addRow(QObject::tr("Lane 状态:"), heartbeatWatchSignalsLabel);
    auto* heartbeatDigestLabel = new QLabel(QStringLiteral("—"), stateGroup);
    heartbeatDigestLabel->setProperty("class", "MonospaceLabel");
    heartbeatDigestLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    stateForm->addRow(QObject::tr("最后决策:"), heartbeatDigestLabel);
    auto* heartbeatStateRefreshBtn = new QPushButton(QObject::tr("刷新状态"), stateGroup);
    stateForm->addRow(QString(), heartbeatStateRefreshBtn);

    stateLayout->addWidget(stateGroup);
    stateLayout->addStretch();
    tabs->addTab(createScrollPage(statePage), QObject::tr("运行状态"));

    auto* schedulerPage = new QWidget();
    auto* schedulerLayout = new QVBoxLayout(schedulerPage);
    schedulerLayout->setContentsMargins(20, 20, 20, 20);
    schedulerLayout->setSpacing(15);

    auto* schedulerGroup = new QGroupBox(QObject::tr("定时任务管理"), schedulerPage);
    schedulerGroup->setProperty("class", "SettingsGroup");
    auto* schedulerInnerLayout = new QVBoxLayout(schedulerGroup);
    schedulerInnerLayout->setContentsMargins(15, 20, 15, 15);
    schedulerInnerLayout->setSpacing(15);

    auto* schedulerTopRow = new QHBoxLayout();
    auto* schedulerJobCombo = new QComboBox(schedulerGroup);
    schedulerJobCombo->setMinimumWidth(300);
    auto* schedulerNewBtn = new QPushButton(QObject::tr("新建"), schedulerGroup);
    auto* schedulerDeleteBtn = new QPushButton(QObject::tr("删除"), schedulerGroup);
    auto* schedulerRunBtn = new QPushButton(QObject::tr("执行"), schedulerGroup);
    schedulerTopRow->addWidget(new QLabel(QObject::tr("任务:"), schedulerGroup));
    schedulerTopRow->addWidget(schedulerJobCombo, 1);
    schedulerTopRow->addWidget(schedulerNewBtn);
    schedulerTopRow->addWidget(schedulerDeleteBtn);
    schedulerTopRow->addWidget(schedulerRunBtn);
    schedulerInnerLayout->addLayout(schedulerTopRow);

    auto* schedulerForm = new QFormLayout();
    schedulerForm->setHorizontalSpacing(15);
    schedulerForm->setVerticalSpacing(10);
    auto* schedulerNameEdit = new QLineEdit(schedulerGroup);
    schedulerForm->addRow(QObject::tr("任务名:"), schedulerNameEdit);
    auto* schedulerAgentCombo = new QComboBox(schedulerGroup);
    schedulerForm->addRow(QObject::tr("执行助手:"), schedulerAgentCombo);
    auto* schedulerCronEdit = new QLineEdit(schedulerGroup);
    schedulerForm->addRow(QObject::tr("Cron 表达式:"), schedulerCronEdit);
    auto* schedulerTimezoneEdit = new QLineEdit(schedulerGroup);
    schedulerForm->addRow(QObject::tr("时区:"), schedulerTimezoneEdit);
    auto* schedulerTargetCombo = new QComboBox(schedulerGroup);
    schedulerTargetCombo->addItem(QObject::tr("主会话"), QStringLiteral("main"));
    schedulerTargetCombo->addItem(QObject::tr("独立会话"), QStringLiteral("isolated"));
    schedulerForm->addRow(QObject::tr("执行目标:"), schedulerTargetCombo);
    auto* schedulerEnabledCheck = new QCheckBox(QObject::tr("启用该任务"), schedulerGroup);
    schedulerForm->addRow(QObject::tr("状态:"), schedulerEnabledCheck);
    auto* schedulerPromptEdit = new QPlainTextEdit(schedulerGroup);
    schedulerPromptEdit->setMinimumHeight(100);
    schedulerForm->addRow(QObject::tr("任务提示词:"), schedulerPromptEdit);
    schedulerInnerLayout->addLayout(schedulerForm);

    auto* schedulerActionRow = new QHBoxLayout();
    auto* schedulerSaveBtn = new QPushButton(QObject::tr("保存任务"), schedulerGroup);
    schedulerActionRow->addStretch();
    schedulerActionRow->addWidget(schedulerSaveBtn);
    schedulerInnerLayout->addLayout(schedulerActionRow);

    schedulerLayout->addWidget(schedulerGroup);
    schedulerLayout->addStretch();
    tabs->addTab(createScrollPage(schedulerPage), QObject::tr("定时任务"));

    auto* footer = new QFrame(&dlg);
    footer->setFixedHeight(60);
    footer->setProperty("class", "SettingsFooter");
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(20, 0, 20, 0);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, footer);
    footerLayout->addStretch();
    footerLayout->addWidget(buttons);
    mainLayout->addWidget(footer);

    bool memoryUiLoading = false;

    auto reloadMemorySettingsUi = [&]() {
        if (!memoryStewardCombo || !memoryStewardModelCombo)
            return;

        memoryUiLoading = true;
        const QString previousStewardId = memoryStewardCombo->currentData().toString().trimmed();
        memoryStewardCombo->clear();
        memoryStewardCombo->addItem(QObject::tr("(不设置)"), QString());

        const QList<Identity*> agents = IdentityManager::instance()->allAgents();
        for (Identity* agent : agents) {
            if (!agent || !agent->isAgent())
                continue;
            const QString name = agent->name().trimmed().isEmpty()
                ? QObject::tr("未命名助手")
                : agent->name().trimmed();
            memoryStewardCombo->addItem(QStringLiteral("%1 (%2)").arg(name, agent->id()), agent->id());
        }

        const QJsonObject policyObj = memory ? memory->loadMemoryPolicyObject(nullptr) : QJsonObject();
        const QString policyStewardId = policyObj.value(QStringLiteral("memory_steward_agent_id")).toString().trimmed();
        QString stewardId = policyStewardId.isEmpty() ? previousStewardId : policyStewardId;
        int stewardIndex = memoryStewardCombo->findData(stewardId);
        if (stewardIndex < 0)
            stewardIndex = 0;
        memoryStewardCombo->setCurrentIndex(stewardIndex);

        const QJsonObject memoryRulesObj = policyObj.value(QStringLiteral("memory_rules")).toObject();
        const bool autoExtractEnabled = memoryRulesObj.value(QStringLiteral("auto_extract_enabled")).toBool(true);
        const int minUserCharsForExtract = qBound(1, memoryRulesObj.value(QStringLiteral("min_user_chars_for_extract")).toInt(12), 4096);
        const int maxLongMemoryCandidates = qBound(1, memoryRulesObj.value(QStringLiteral("max_long_memory_candidates_per_turn")).toInt(3), 32);
        const bool reflectEnabled = memoryRulesObj.value(QStringLiteral("reflect_enabled")).toBool(true);
        const int reflectEveryNTurns = qBound(1, memoryRulesObj.value(QStringLiteral("reflect_every_n_turns")).toInt(8), 200);
        const int reflectMaxCandidates = qBound(1, memoryRulesObj.value(QStringLiteral("reflect_max_candidates_per_run")).toInt(4), 32);
        const int reflectScanDailyFiles = qBound(1, memoryRulesObj.value(QStringLiteral("reflect_scan_daily_files")).toInt(7), 30);

        memoryAutoExtractCheck->setChecked(autoExtractEnabled);
        memoryMinCharsSpin->setValue(minUserCharsForExtract);
        memoryMaxCandidatesSpin->setValue(maxLongMemoryCandidates);
        memoryReflectCheck->setChecked(reflectEnabled);
        memoryReflectEveryTurnsSpin->setValue(reflectEveryNTurns);
        memoryReflectMaxCandidatesSpin->setValue(reflectMaxCandidates);
        memoryReflectScanDailyFilesSpin->setValue(reflectScanDailyFiles);
        memoryMinCharsSpin->setEnabled(autoExtractEnabled);
        memoryMaxCandidatesSpin->setEnabled(autoExtractEnabled);
        memoryReflectEveryTurnsSpin->setEnabled(reflectEnabled);
        memoryReflectMaxCandidatesSpin->setEnabled(reflectEnabled);
        memoryReflectScanDailyFilesSpin->setEnabled(reflectEnabled);

        const QStringList configIds = governance
            ? governance->registeredModelConfigIds()
            : QStringList();
        memoryStewardModelCombo->clear();
        for (const QString& cid : configIds)
            memoryStewardModelCombo->addItem(cid, cid);

        const QString selectedStewardId = memoryStewardCombo->currentData().toString().trimmed();
        QString stewardConfigId;
        if (!selectedStewardId.isEmpty()) {
            Identity* steward = IdentityManager::instance()->findById(selectedStewardId);
            if (steward && steward->profile()) {
                const LLMConfig cfg = steward->profile()->llmConfig();
                stewardConfigId = ModelFactory::resolveConfigKey(cfg);
            }
        }
        if (stewardConfigId.isEmpty() && governance)
            stewardConfigId = ModelFactory::resolveConfigKey(governance->defaultAgentConfig());
        int modelIndex = memoryStewardModelCombo->findData(stewardConfigId);
        if (modelIndex < 0 && memoryStewardModelCombo->count() > 0)
            modelIndex = 0;
        if (modelIndex >= 0)
            memoryStewardModelCombo->setCurrentIndex(modelIndex);

        const QString userMd = memory ? memory->loadUserMemoryMarkdown(nullptr) : QString();
        const QHash<QString, QString> fields = parseUserProfileFields(userMd);
        userPreferredNameEdit->setText(fields.value(QStringLiteral("preferred_name")));
        userIdentityEdit->setText(fields.value(QStringLiteral("self_identity")));
        userGoalsEdit->setPlainText(fields.value(QStringLiteral("focus_goals")));
        userPreferencesEdit->setPlainText(fields.value(QStringLiteral("preference_traits")));
        companyCultureEdit->setPlainText(fields.value(QStringLiteral("company_culture")));
        userNotesEdit->setPlainText(parseUserNotesSection(userMd));
        memoryUiLoading = false;
    };

    auto saveMemorySettingsUi = [&](QString* error) -> bool {
        if (error)
            error->clear();
        if (!memory || !governance || !workspace) {
            if (error)
                *error = QObject::tr("配置服务不可用");
            return false;
        }

        const QString stewardId = memoryStewardCombo->currentData().toString().trimmed();
        const QString stewardModelId = memoryStewardModelCombo->currentData().toString().trimmed();

        QJsonObject policyObj = memory ? memory->loadMemoryPolicyObject(nullptr) : QJsonObject();
        policyObj.insert(QStringLiteral("memory_steward_agent_id"), stewardId);
        QJsonObject memoryRulesObj = policyObj.value(QStringLiteral("memory_rules")).toObject();
        memoryRulesObj.insert(QStringLiteral("auto_extract_enabled"), memoryAutoExtractCheck->isChecked());
        memoryRulesObj.insert(QStringLiteral("min_user_chars_for_extract"), memoryMinCharsSpin->value());
        memoryRulesObj.insert(QStringLiteral("max_long_memory_candidates_per_turn"), memoryMaxCandidatesSpin->value());
        memoryRulesObj.insert(QStringLiteral("reflect_enabled"), memoryReflectCheck->isChecked());
        memoryRulesObj.insert(QStringLiteral("reflect_every_n_turns"), memoryReflectEveryTurnsSpin->value());
        memoryRulesObj.insert(QStringLiteral("reflect_max_candidates_per_run"), memoryReflectMaxCandidatesSpin->value());
        memoryRulesObj.insert(QStringLiteral("reflect_scan_daily_files"), memoryReflectScanDailyFilesSpin->value());
        policyObj.insert(QStringLiteral("memory_rules"), memoryRulesObj);
        if (!memory->saveMemoryPolicyObject(policyObj)) {
            if (error)
                *error = QObject::tr("写入 memory_policy.json 失败");
            return false;
        }

        if (!stewardId.isEmpty() && !stewardModelId.isEmpty()) {
            Identity* steward = IdentityManager::instance()->findById(stewardId);
            if (steward && steward->isAgent() && steward->profile()) {
                LLMConfig cfg = steward->profile()->llmConfig();
                cfg.configId = stewardModelId;
                steward->profile()->setLlmConfig(cfg);
            }
        }

        QHash<QString, QString> fields;
        fields.insert(QStringLiteral("preferred_name"), userPreferredNameEdit->text());
        fields.insert(QStringLiteral("self_identity"), userIdentityEdit->text());
        fields.insert(QStringLiteral("focus_goals"), userGoalsEdit->toPlainText());
        fields.insert(QStringLiteral("preference_traits"), userPreferencesEdit->toPlainText());
        fields.insert(QStringLiteral("company_culture"), companyCultureEdit->toPlainText());
        fields.insert(QStringLiteral("communication_style"), fields.value(QStringLiteral("preference_traits")));
        fields.insert(QStringLiteral("long_term_preferences"), fields.value(QStringLiteral("focus_goals")));
        const QString userMarkdown = buildUserProfileMarkdown(fields, userNotesEdit->toPlainText());

        QString userSaveError;
        if (!memory->saveUserMemoryMarkdown(userMarkdown, &userSaveError)) {
            if (error)
                *error = userSaveError.isEmpty() ? QObject::tr("写入 user.md 失败") : userSaveError;
            return false;
        }

        governance->applyConfigToAllRuntimes();
        workspace->saveSessionsToDisk();
        return true;
    };

    auto onMemoryStewardChanged = [&]() {
        if (memoryUiLoading)
            return;
        const QString stewardId = memoryStewardCombo->currentData().toString().trimmed();
        if (stewardId.isEmpty() || !governance)
            return;
        Identity* steward = IdentityManager::instance()->findById(stewardId);
        if (!steward || !steward->profile())
            return;
        const LLMConfig cfg = steward->profile()->llmConfig();
        const QString cfgId = ModelFactory::resolveConfigKey(cfg);
        const int idx = memoryStewardModelCombo->findData(cfgId);
        if (idx >= 0)
            memoryStewardModelCombo->setCurrentIndex(idx);
    };

    const QList<Identity*> agents = IdentityManager::instance()->allAgents();
    for (Identity* agent : agents) {
        if (!agent || !agent->isAgent())
            continue;
        const QString displayName = agent->name().trimmed().isEmpty()
            ? QObject::tr("未命名助手")
            : agent->name().trimmed();
        const QString itemText = QStringLiteral("%1 (%2)").arg(displayName, agent->id());
        heartbeatAgentCombo->addItem(itemText, agent->id());
        schedulerAgentCombo->addItem(itemText, agent->id());
    }

    auto refreshHeartbeatStateUiForSelected = [=]() {
        const QString agentId = heartbeatAgentCombo->currentData().toString().trimmed();
        if (agentId.isEmpty()) {
            heartbeatStatePathLabel->setText(QObject::tr("未选择助手"));
            heartbeatLastSnapshotLabel->setText(QStringLiteral("—"));
            heartbeatLastNotifyLabel->setText(QStringLiteral("—"));
            heartbeatLastChangeLabel->setText(QStringLiteral("—"));
            heartbeatReasonLabel->setText(QStringLiteral("—"));
            heartbeatJobsLabel->setText(QStringLiteral("0"));
            heartbeatProviderDownLabel->setText(QStringLiteral("unknown"));
            heartbeatWatchSignalsLabel->setText(QStringLiteral("—"));
            heartbeatDigestLabel->setText(QStringLiteral("—"));
            return;
        }

        const QString statePath = memory ? memory->heartbeatRuntimeStateLocation(agentId) : QString();
        heartbeatStatePathLabel->setText(statePath);
        bool ok = false;
        const QJsonObject state = memory ? memory->loadHeartbeatRuntimeState(agentId, &ok) : QJsonObject();
        if (!ok || state.isEmpty()) {
            const QString pending = QObject::tr("暂无（等待首次心跳）");
            heartbeatLastSnapshotLabel->setText(pending);
            heartbeatLastNotifyLabel->setText(pending);
            heartbeatLastChangeLabel->setText(pending);
            heartbeatReasonLabel->setText(QStringLiteral("—"));
            heartbeatJobsLabel->setText(QStringLiteral("0"));
            heartbeatProviderDownLabel->setText(QStringLiteral("unknown"));
            heartbeatWatchSignalsLabel->setText(QStringLiteral("idle"));
            heartbeatDigestLabel->setText(QStringLiteral("—"));
            return;
        }

        heartbeatLastSnapshotLabel->setText(utcFieldToLocalText(state, QStringLiteral("last_completed_at_utc")));
        heartbeatLastNotifyLabel->setText(utcFieldToLocalText(state, QStringLiteral("last_delivered_at_utc")));
        heartbeatLastChangeLabel->setText(utcFieldToLocalText(state, QStringLiteral("next_due_at_utc")));
        const QString reason = state.value(QStringLiteral("last_deferred_reason")).toString().trimmed();
        heartbeatReasonLabel->setText(reason.isEmpty() ? QStringLiteral("—") : reason);
        heartbeatJobsLabel->setText(state.value(QStringLiteral("has_pending_ticket")).toBool(false)
                                        ? QObject::tr("1")
                                        : QObject::tr("0"));
        heartbeatProviderDownLabel->setText(
            state.value(QStringLiteral("provider_state")).toString().trimmed().isEmpty()
                ? QStringLiteral("unknown")
                : state.value(QStringLiteral("provider_state")).toString().trimmed());
        heartbeatWatchSignalsLabel->setText(
            state.value(QStringLiteral("lane_state")).toString().trimmed().isEmpty()
                ? QStringLiteral("idle")
                : state.value(QStringLiteral("lane_state")).toString().trimmed());
        const QString decision = state.value(QStringLiteral("last_decision")).toString().trimmed();
        heartbeatDigestLabel->setText(decision.isEmpty() ? QStringLiteral("—") : decision);
        heartbeatDigestLabel->setToolTip(decision);
    };

    auto loadHeartbeatUiForSelected = [=]() {
        const QString agentId = heartbeatAgentCombo->currentData().toString().trimmed();
        if (agentId.isEmpty()) {
            heartbeatEnabledCheck->setChecked(true);
            heartbeatIntervalSpin->setValue(30 * 60);
            heartbeatSilentNoChangeCheck->setChecked(true);
            heartbeatNotifyOnChangeOnlyCheck->setChecked(true);
            heartbeatNotifyIntervalSpin->setValue(5);
            heartbeatPersistNoChangeCheck->setChecked(true);
            heartbeatStatePersistIntervalSpin->setValue(250);
            heartbeatStartEdit->setText(QStringLiteral("08:00"));
            heartbeatEndEdit->setText(QStringLiteral("23:00"));
            heartbeatTimezoneEdit->setText(QString::fromUtf8(QTimeZone::systemTimeZoneId()));
            heartbeatSignalProviderCheck->setChecked(true);
            heartbeatSignalDelegateCheck->setChecked(true);
            heartbeatSignalPulseCheck->setChecked(true);
            heartbeatSignalSchedulerCheck->setChecked(true);
            heartbeatSignalMemoryCheck->setChecked(true);
            heartbeatSignalExtraEdit->clear();
            heartbeatInstructionEdit->setPlainText(QString());
            heartbeatInstructionEdit->setProperty("heartbeatPath", QString());
            heartbeatPathLabel->setText(QObject::tr("未选择助手"));
            refreshHeartbeatStateUiForSelected();
            return;
        }

        const HeartbeatPolicy policy = memory ? memory->heartbeatPolicyForAgent(agentId) : HeartbeatPolicy {};
        heartbeatEnabledCheck->setChecked(policy.enabled);
        heartbeatIntervalSpin->setValue(qMax(5, policy.cadenceMs / 1000));
        heartbeatSilentNoChangeCheck->setChecked(policy.deliveryPolicy.deliverActionableSummary);
        heartbeatNotifyOnChangeOnlyCheck->setChecked(policy.llmEscalation.enabled);
        heartbeatNotifyIntervalSpin->setValue(qMax(0, policy.startupGraceMs / 1000));
        heartbeatPersistNoChangeCheck->setChecked(policy.maintenancePolicy.reflectMemory);
        heartbeatStatePersistIntervalSpin->setValue(qMax(10, policy.coalesceMs));
        heartbeatStartEdit->setText(policy.activeHours.start.isValid() ? policy.activeHours.start.toString(QStringLiteral("HH:mm")) : QStringLiteral("08:00"));
        heartbeatEndEdit->setText(policy.activeHours.end.isValid() ? policy.activeHours.end.toString(QStringLiteral("HH:mm")) : QStringLiteral("23:00"));
        heartbeatTimezoneEdit->setText(policy.activeHours.timezone.trimmed().isEmpty() ? QString::fromUtf8(QTimeZone::systemTimeZoneId()) : policy.activeHours.timezone.trimmed());

        heartbeatSignalProviderCheck->setChecked(policy.watchModules.provider);
        heartbeatSignalDelegateCheck->setChecked(policy.watchModules.delegateJobs);
        heartbeatSignalPulseCheck->setChecked(policy.watchModules.pulse);
        heartbeatSignalSchedulerCheck->setChecked(policy.watchModules.scheduler);
        heartbeatSignalMemoryCheck->setChecked(policy.watchModules.memory);

        heartbeatSignalExtraEdit->clear();

        QString path = policy.instructionPath.trimmed();
        if (path.isEmpty())
            path = memory ? memory->heartbeatInstructionPathForAgent(agentId).trimmed() : QString();
        if (path.isEmpty())
            path = memory ? memory->agentHeartbeatInstructionPath(agentId) : QString();
        heartbeatInstructionEdit->setProperty("heartbeatPath", path);
        heartbeatPathLabel->setText(path);
        heartbeatInstructionEdit->setPlainText(memory ? memory->readPossiblyMojibakeUtf8File(path, nullptr) : QString());
        refreshHeartbeatStateUiForSelected();
    };

    auto applyHeartbeatUiForSelected = [=](bool showToast) -> bool {
        const QString agentId = heartbeatAgentCombo->currentData().toString().trimmed();
        if (agentId.isEmpty())
            return true;

        HeartbeatPolicy policy = memory ? memory->heartbeatPolicyForAgent(agentId) : HeartbeatPolicy {};
        policy.enabled = heartbeatEnabledCheck->isChecked();
        policy.cadenceMs = qMax(1000, heartbeatIntervalSpin->value() * 1000);
        policy.deliveryPolicy.deliverActionableSummary = heartbeatSilentNoChangeCheck->isChecked();
        policy.llmEscalation.enabled = heartbeatNotifyOnChangeOnlyCheck->isChecked();
        policy.startupGraceMs = qMax(0, heartbeatNotifyIntervalSpin->value() * 1000);
        policy.maintenancePolicy.reflectMemory = heartbeatPersistNoChangeCheck->isChecked();
        policy.maintenancePolicy.rebuildMemoryIndex = heartbeatPersistNoChangeCheck->isChecked();
        policy.coalesceMs = qMax(10, heartbeatStatePersistIntervalSpin->value());

        policy.watchModules.provider = heartbeatSignalProviderCheck->isChecked();
        policy.watchModules.delegateJobs = heartbeatSignalDelegateCheck->isChecked();
        policy.watchModules.pulse = heartbeatSignalPulseCheck->isChecked();
        policy.watchModules.scheduler = heartbeatSignalSchedulerCheck->isChecked();
        policy.watchModules.memory = heartbeatSignalMemoryCheck->isChecked();
        policy.actionableRules.providerStatus = policy.watchModules.provider;
        policy.actionableRules.delegateChanges = policy.watchModules.delegateJobs;
        policy.actionableRules.pulseRisk = policy.watchModules.pulse;
        policy.actionableRules.schedulerIssues = policy.watchModules.scheduler;
        policy.actionableRules.memoryIssues = policy.watchModules.memory;

        const QTime startParsed = QTime::fromString(heartbeatStartEdit->text().trimmed(), QStringLiteral("HH:mm"));
        const QTime endParsed = QTime::fromString(heartbeatEndEdit->text().trimmed(), QStringLiteral("HH:mm"));
        policy.activeHours.start = startParsed.isValid() ? startParsed : QTime(8, 0);
        policy.activeHours.end = endParsed.isValid() ? endParsed : QTime(23, 0);
        policy.activeHours.timezone = heartbeatTimezoneEdit->text().trimmed();
        if (policy.activeHours.timezone.isEmpty())
            policy.activeHours.timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());

        QString path = heartbeatInstructionEdit->property("heartbeatPath").toString().trimmed();
        if (path.isEmpty())
            path = policy.instructionPath.trimmed();
        if (path.isEmpty())
            path = memory ? memory->agentHeartbeatInstructionPath(agentId) : QString();
        policy.instructionPath = path;
        heartbeatPathLabel->setText(path);

        QString heartbeatWriteError;
        if (!memory
            || !memory->writeUtf8TextFile(path, heartbeatInstructionEdit->toPlainText(), &heartbeatWriteError)) {
            if (showToast) {
                QMessageBox::warning(
                    parent,
                    QObject::tr("保存失败"),
                    heartbeatWriteError.isEmpty()
                        ? QObject::tr("写入心跳内容失败：%1").arg(path)
                        : QStringLiteral("%1：%2").arg(heartbeatWriteError, path));
            }
            return false;
        }

        if (memory)
            memory->updateHeartbeatPolicy(agentId, policy);
        if (memory) {
            if (policy.enabled)
                memory->startAgentHeartbeat(agentId);
            else
                memory->stopAgentHeartbeat(agentId);
        }
        refreshHeartbeatStateUiForSelected();
        if (showToast)
            QMessageBox::information(parent, QObject::tr("保存成功"), QObject::tr("心跳配置已更新。"));
        return true;
    };

    auto loadSchedulerJobToForm = [=](const QString& jobId) {
        if (jobId.trimmed().isEmpty()) {
            schedulerNameEdit->clear();
            schedulerCronEdit->setText(QStringLiteral("0 9 * * *"));
            schedulerTimezoneEdit->setText(QString::fromUtf8(QTimeZone::systemTimeZoneId()));
            schedulerTargetCombo->setCurrentIndex(0);
            schedulerEnabledCheck->setChecked(true);
            schedulerPromptEdit->clear();
            if (schedulerAgentCombo->count() > 0)
                schedulerAgentCombo->setCurrentIndex(0);
            return;
        }
        ScheduledJob job;
        if (!memory || !memory->scheduledJobById(jobId, &job))
            return;
        schedulerNameEdit->setText(job.name);
        int agentIdx = schedulerAgentCombo->findData(job.agentId);
        if (agentIdx < 0 && schedulerAgentCombo->count() > 0)
            agentIdx = 0;
        if (agentIdx >= 0)
            schedulerAgentCombo->setCurrentIndex(agentIdx);
        schedulerCronEdit->setText(job.cronExpr);
        schedulerTimezoneEdit->setText(job.timezone.trimmed().isEmpty()
                                           ? QString::fromUtf8(QTimeZone::systemTimeZoneId())
                                           : job.timezone.trimmed());
        int targetIdx = schedulerTargetCombo->findData(job.sessionTarget.trimmed());
        if (targetIdx < 0)
            targetIdx = 0;
        schedulerTargetCombo->setCurrentIndex(targetIdx);
        schedulerEnabledCheck->setChecked(job.enabled);
        schedulerPromptEdit->setPlainText(job.prompt);
    };

    auto reloadSchedulerJobs = [=](const QString& selectId) {
        QString selectedId = selectId.trimmed();
        if (selectedId.isEmpty())
            selectedId = schedulerJobCombo->currentData().toString().trimmed();

        schedulerJobCombo->blockSignals(true);
        schedulerJobCombo->clear();
        schedulerJobCombo->addItem(QObject::tr("(新建任务)"), QString());

        QList<ScheduledJob> jobs = memory ? memory->allScheduledJobs() : QList<ScheduledJob>();
        std::sort(jobs.begin(), jobs.end(), [](const ScheduledJob& a, const ScheduledJob& b) {
            const QString an = a.name.trimmed().isEmpty() ? a.jobId : a.name.trimmed();
            const QString bn = b.name.trimmed().isEmpty() ? b.jobId : b.name.trimmed();
            const int byName = an.localeAwareCompare(bn);
            if (byName != 0)
                return byName < 0;
            return a.jobId < b.jobId;
        });

        for (const ScheduledJob& job : jobs) {
            const QString name = job.name.trimmed().isEmpty() ? QObject::tr("未命名任务") : job.name.trimmed();
            schedulerJobCombo->addItem(QStringLiteral("%1 [%2]").arg(name, job.jobId.left(8)), job.jobId);
        }

        int idx = schedulerJobCombo->findData(selectedId);
        if (idx < 0)
            idx = 0;
        schedulerJobCombo->setCurrentIndex(idx);
        schedulerJobCombo->blockSignals(false);
        loadSchedulerJobToForm(schedulerJobCombo->currentData().toString().trimmed());
    };

    QObject::connect(memoryStewardCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [=](int) {
        onMemoryStewardChanged();
    });
    QObject::connect(memoryAutoExtractCheck, &QCheckBox::toggled, &dlg, [=](bool enabled) {
        memoryMinCharsSpin->setEnabled(enabled);
        memoryMaxCandidatesSpin->setEnabled(enabled);
    });
    QObject::connect(memoryReflectCheck, &QCheckBox::toggled, &dlg, [=](bool enabled) {
        memoryReflectEveryTurnsSpin->setEnabled(enabled);
        memoryReflectMaxCandidatesSpin->setEnabled(enabled);
        memoryReflectScanDailyFilesSpin->setEnabled(enabled);
    });
    QObject::connect(memoryReindexBtn, &QPushButton::clicked, &dlg, [=]() {
        const QString userId = IdentityManager::instance()->userIdentity()->id();
        QJsonObject rebuildResult;
        QString rebuildError;
        const bool ok = memory->rebuildMemoryIndexAs(userId, QString(), &rebuildResult, &rebuildError);
        const int total = rebuildResult.value(QStringLiteral("agents_total")).toInt();
        const int success = rebuildResult.value(QStringLiteral("agents_success")).toInt();
        const int failed = rebuildResult.value(QStringLiteral("agents_failed")).toInt();
        const int rows = rebuildResult.value(QStringLiteral("rows_indexed")).toInt();
        QString summary = QObject::tr("助手总数: %1\n成功: %2\n失败: %3\n索引行数: %4").arg(total).arg(success).arg(failed).arg(rows);
        if (!rebuildError.trimmed().isEmpty())
            summary += QStringLiteral("\n\n") + rebuildError.trimmed();
        if (ok)
            QMessageBox::information(parent, QObject::tr("索引重建完成"), summary);
        else
            QMessageBox::warning(parent, QObject::tr("索引重建部分失败"), summary);
    });

    QObject::connect(heartbeatAgentCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [=](int) {
        loadHeartbeatUiForSelected();
    });
    QObject::connect(heartbeatApplyBtn, &QPushButton::clicked, &dlg, [=]() { applyHeartbeatUiForSelected(true); });
    QObject::connect(heartbeatTriggerBtn, &QPushButton::clicked, &dlg, [=, &dlg]() {
        const QString agentId = heartbeatAgentCombo->currentData().toString().trimmed();
        if (agentId.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("触发失败"), QObject::tr("请先选择一个助手。"));
            return;
        }
        if (memory)
            memory->requestManualHeartbeat(agentId, QStringLiteral("manual_ui"));
        QMessageBox::information(parent, QObject::tr("已触发"), QObject::tr("已触发心跳任务。"));
        QTimer::singleShot(800, &dlg, [=]() { refreshHeartbeatStateUiForSelected(); });
    });
    QObject::connect(heartbeatStateRefreshBtn, &QPushButton::clicked, &dlg, [=]() { refreshHeartbeatStateUiForSelected(); });

    QObject::connect(schedulerJobCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [=](int) {
        loadSchedulerJobToForm(schedulerJobCombo->currentData().toString());
    });
    QObject::connect(schedulerNewBtn, &QPushButton::clicked, &dlg, [=]() {
        schedulerJobCombo->setCurrentIndex(0);
        loadSchedulerJobToForm(QString());
    });
    QObject::connect(schedulerSaveBtn, &QPushButton::clicked, &dlg, [=]() {
        const QString agentId = schedulerAgentCombo->currentData().toString().trimmed();
        const QString prompt = schedulerPromptEdit->toPlainText().trimmed();
        const QString cronExpr = schedulerCronEdit->text().simplified();
        if (agentId.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("请先选择执行助手。"));
            return;
        }
        if (prompt.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("任务内容不能为空。"));
            return;
        }
        if (cronExpr.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() != 5) {
            QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("Cron 表达式格式错误，需要 5 段。"));
            return;
        }

        ScheduledJob job;
        job.name = schedulerNameEdit->text().trimmed();
        if (job.name.isEmpty())
            job.name = QObject::tr("定时任务");
        job.agentId = agentId;
        job.prompt = prompt;
        job.cronExpr = cronExpr;
        job.timezone = schedulerTimezoneEdit->text().trimmed();
        if (job.timezone.isEmpty())
            job.timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
        job.sessionTarget = schedulerTargetCombo->currentData().toString().trimmed();
        if (job.sessionTarget.isEmpty())
            job.sessionTarget = QStringLiteral("main");
        job.enabled = schedulerEnabledCheck->isChecked();

        const QString jobId = schedulerJobCombo->currentData().toString().trimmed();
        if (jobId.isEmpty()) {
            const QString newId = memory ? memory->addScheduledJob(job) : QString();
            if (newId.trimmed().isEmpty()) {
                QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("创建任务失败。"));
                return;
            }
            reloadSchedulerJobs(newId);
        } else {
            if (!memory || !memory->updateScheduledJob(jobId, job)) {
                QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("更新任务失败。"));
                return;
            }
            reloadSchedulerJobs(jobId);
        }
        QMessageBox::information(parent, QObject::tr("保存成功"), QObject::tr("定时任务已更新。"));
    });
    QObject::connect(schedulerDeleteBtn, &QPushButton::clicked, &dlg, [=]() {
        const QString jobId = schedulerJobCombo->currentData().toString().trimmed();
        if (jobId.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("删除失败"), QObject::tr("请先选择一个已有任务。"));
            return;
        }
        if (QMessageBox::question(parent, QObject::tr("删除任务"), QObject::tr("确认删除该定时任务？")) != QMessageBox::Yes)
            return;
        if (!memory || !memory->removeScheduledJob(jobId)) {
            QMessageBox::warning(parent, QObject::tr("删除失败"), QObject::tr("删除任务失败。"));
            return;
        }
        reloadSchedulerJobs(QString());
    });
    QObject::connect(schedulerRunBtn, &QPushButton::clicked, &dlg, [=]() {
        const QString jobId = schedulerJobCombo->currentData().toString().trimmed();
        if (jobId.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("执行失败"), QObject::tr("请先选择一个已有任务。"));
            return;
        }
        if (memory)
            memory->triggerScheduledJob(jobId);
        QMessageBox::information(parent, QObject::tr("已触发"), QObject::tr("已触发定时任务。"));
    });

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [=, &dlg]() {
        if (canManageGlobalConfig) {
            QString err;
            if (!saveMemorySettingsUi(&err)) {
                QMessageBox::warning(parent, QObject::tr("保存失败"), err.isEmpty() ? QObject::tr("信息设置保存失败。") : err);
                return;
            }
        }
        if (isAgentIdentity && activeIdentity && activeIdentity->profile() && agentToolEditor && agentDelegateCheck) {
            QStringList selectedTools = agentToolEditor->selectedTools();
            selectedTools.removeDuplicates();
            if (!agentDelegateCheck->isChecked()) {
                for (const QString& toolName : AgentToolNames::all())
                    selectedTools.removeAll(toolName);
            }
            activeIdentity->profile()->setDelegateEnabled(agentDelegateCheck->isChecked());
            activeIdentity->profile()->setAllowedTools(selectedTools);
            governance->applyToolDispatcherToAllRuntimes();
            if (workspace)
                workspace->saveSessionsToDisk();
        }
        if (canManageGlobalConfig) {
            if (!applyHeartbeatUiForSelected(false)) {
                QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("心跳配置保存失败。"));
                return;
            }
        }
        QMessageBox::information(parent, QObject::tr("保存成功"), QObject::tr("信息设置已更新。"));
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    reloadMemorySettingsUi();
    if (isAgentIdentity && activeIdentity && activeIdentity->profile() && agentToolEditor && agentDelegateCheck) {
        agentDelegateCheck->setChecked(activeIdentity->profile()->delegateEnabled());
        agentToolEditor->setToolPlugins(governance->toolPluginInfos());
        agentToolEditor->setSelectedTools(activeIdentity->profile()->allowedTools());
    }
    loadHeartbeatUiForSelected();
    auto* heartbeatStateTimer = new QTimer(&dlg);
    heartbeatStateTimer->setInterval(3000);
    QObject::connect(heartbeatStateTimer, &QTimer::timeout, &dlg, [=]() { refreshHeartbeatStateUiForSelected(); });
    heartbeatStateTimer->start();
    reloadSchedulerJobs(QString());

    if (!canManageGlobalConfig) {
        memoryGroup->setEnabled(false);
        userGroup->setEnabled(false);
        heartbeatGroup->setEnabled(false);
        schedulerGroup->setEnabled(false);
    }

    dlg.exec();
}

} // namespace InformationSettingsDialog
