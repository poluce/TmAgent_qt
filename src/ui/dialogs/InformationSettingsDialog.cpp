#include "InformationSettingsDialog.h"

#include "core/manager/IdentityManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
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

QString normalizeHeartbeatSignalName(const QString& raw)
{
    const QString s = raw.trimmed().toLower();
    if (s == QLatin1String("provider") || s == QLatin1String("provider_status"))
        return QStringLiteral("provider_status");
    if (s == QLatin1String("delegate") || s == QLatin1String("delegate_jobs"))
        return QStringLiteral("delegate_jobs");
    if (s == QLatin1String("pulse") || s == QLatin1String("pulse_state"))
        return QStringLiteral("pulse_state");
    if (s == QLatin1String("scheduler") || s == QLatin1String("scheduler_jobs"))
        return QStringLiteral("scheduler_jobs");
    if (s == QLatin1String("memory") || s == QLatin1String("memory_progress"))
        return QStringLiteral("memory_progress");
    return s;
}

QStringList normalizeHeartbeatSignalNames(const QStringList& rawSignals)
{
    QStringList out;
    for (const QString& raw : rawSignals) {
        const QString normalized = normalizeHeartbeatSignalName(raw);
        if (normalized.isEmpty())
            continue;
        if (!out.contains(normalized))
            out.append(normalized);
    }
    if (out.isEmpty()) {
        out << QStringLiteral("provider_status")
            << QStringLiteral("delegate_jobs")
            << QStringLiteral("pulse_state");
    }
    return out;
}

QStringList parseHeartbeatSignalInput(const QString& rawInput)
{
    const QStringList parts = rawInput.split(QRegularExpression(QStringLiteral("[,;\\n]")), Qt::SkipEmptyParts);
    QStringList out;
    for (const QString& part : parts) {
        const QString normalized = normalizeHeartbeatSignalName(part);
        if (normalized.isEmpty())
            continue;
        if (!out.contains(normalized))
            out.append(normalized);
    }
    return out;
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

void show(QWidget* parent, const InformationSettingsCapabilities& capabilities, const QString& activeIdentityId)
{
    if (!capabilities.persistence
        || !capabilities.memoryCommands
        || !capabilities.governanceCommands
        || !capabilities.governanceQueries
        || !capabilities.modelCatalog)
        return;

    const bool canManageGlobalConfig = capabilities.canManageGlobalConfig
        ? capabilities.canManageGlobalConfig(activeIdentityId)
        : false;

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

    auto* heartbeatPage = new QWidget();
    auto* heartbeatLayout = new QVBoxLayout(heartbeatPage);
    heartbeatLayout->setContentsMargins(20, 20, 20, 20);
    heartbeatLayout->setSpacing(15);

    auto* heartbeatGroup = new QGroupBox(QObject::tr("巡检循环配置"), heartbeatPage);
    heartbeatGroup->setProperty("class", "SettingsGroup");
    auto* heartbeatForm = new QFormLayout(heartbeatGroup);
    heartbeatForm->setContentsMargins(15, 20, 15, 15);
    heartbeatForm->setHorizontalSpacing(15);
    heartbeatForm->setVerticalSpacing(10);

    auto* heartbeatAgentCombo = new QComboBox(heartbeatGroup);
    heartbeatAgentCombo->setMinimumWidth(300);
    heartbeatForm->addRow(QObject::tr("执行助手:"), heartbeatAgentCombo);

    auto* heartbeatEnabledCheck = new QCheckBox(QObject::tr("启用心跳循环"), heartbeatGroup);
    heartbeatForm->addRow(QObject::tr("状态开关:"), heartbeatEnabledCheck);

    auto* heartbeatIntervalSpin = new QSpinBox(heartbeatGroup);
    heartbeatIntervalSpin->setRange(5, 24 * 60 * 60);
    heartbeatIntervalSpin->setSuffix(QObject::tr(" 秒"));
    heartbeatForm->addRow(QObject::tr("采样间隔:"), heartbeatIntervalSpin);

    auto* hbNotifyBox = new QWidget(heartbeatGroup);
    auto* hbNotifyLayout = new QVBoxLayout(hbNotifyBox);
    hbNotifyLayout->setContentsMargins(0, 0, 0, 0);
    auto* heartbeatSilentNoChangeCheck = new QCheckBox(QObject::tr("无变化时静默"), hbNotifyBox);
    auto* heartbeatNotifyOnChangeOnlyCheck = new QCheckBox(QObject::tr("仅在状态变化时通知"), hbNotifyBox);
    hbNotifyLayout->addWidget(heartbeatSilentNoChangeCheck);
    hbNotifyLayout->addWidget(heartbeatNotifyOnChangeOnlyCheck);
    heartbeatForm->addRow(QObject::tr("通知策略:"), hbNotifyBox);

    auto* heartbeatNotifyIntervalSpin = new QSpinBox(heartbeatGroup);
    heartbeatNotifyIntervalSpin->setRange(1, 1440);
    heartbeatNotifyIntervalSpin->setSuffix(QObject::tr(" 分钟"));
    heartbeatNotifyIntervalSpin->setValue(30);
    heartbeatNotifyIntervalSpin->setToolTip(
        QObject::tr("无变化时允许通知的最小间隔。启用静默策略后，此项主要作为保底限频。"));
    heartbeatForm->addRow(QObject::tr("通知最小间隔:"), heartbeatNotifyIntervalSpin);

    auto* heartbeatPersistNoChangeCheck = new QCheckBox(QObject::tr("无变化时也持久化状态"), heartbeatGroup);
    heartbeatPersistNoChangeCheck->setChecked(false);
    heartbeatPersistNoChangeCheck->setToolTip(
        QObject::tr("关闭时仅在有变化、触发通知或达到最低落盘间隔时持久化心跳状态。"));
    heartbeatForm->addRow(QObject::tr("落盘策略:"), heartbeatPersistNoChangeCheck);

    auto* heartbeatStatePersistIntervalSpin = new QSpinBox(heartbeatGroup);
    heartbeatStatePersistIntervalSpin->setRange(1, 3600);
    heartbeatStatePersistIntervalSpin->setSuffix(QObject::tr(" 秒"));
    heartbeatStatePersistIntervalSpin->setValue(60);
    heartbeatStatePersistIntervalSpin->setToolTip(QObject::tr("无变化场景下心跳状态的最低持久化间隔。"));
    heartbeatForm->addRow(QObject::tr("状态落盘间隔:"), heartbeatStatePersistIntervalSpin);

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
    auto* heartbeatSignalDelegateCheck = new QCheckBox(QObject::tr("子任务"), heartbeatSignalsRow);
    auto* heartbeatSignalPulseCheck = new QCheckBox(QObject::tr("状态"), heartbeatSignalsRow);
    auto* heartbeatSignalSchedulerCheck = new QCheckBox(QObject::tr("定时"), heartbeatSignalsRow);
    auto* heartbeatSignalMemoryCheck = new QCheckBox(QObject::tr("记忆"), heartbeatSignalsRow);
    heartbeatSignalsLayout->addWidget(heartbeatSignalProviderCheck);
    heartbeatSignalsLayout->addWidget(heartbeatSignalDelegateCheck);
    heartbeatSignalsLayout->addWidget(heartbeatSignalPulseCheck);
    heartbeatSignalsLayout->addWidget(heartbeatSignalSchedulerCheck);
    heartbeatSignalsLayout->addWidget(heartbeatSignalMemoryCheck);
    heartbeatForm->addRow(QObject::tr("快照信号:"), heartbeatSignalsRow);

    auto* heartbeatSignalExtraEdit = new QLineEdit(heartbeatGroup);
    heartbeatForm->addRow(QObject::tr("扩展信号:"), heartbeatSignalExtraEdit);

    auto* heartbeatPathLabel = new QLabel(heartbeatGroup);
    heartbeatPathLabel->setProperty("class", "PathLabel");
    heartbeatPathLabel->setWordWrap(true);
    heartbeatForm->addRow(QObject::tr("心跳文件:"), heartbeatPathLabel);

    auto* heartbeatInstructionEdit = new QPlainTextEdit(heartbeatGroup);
    heartbeatInstructionEdit->setMinimumHeight(120);
    heartbeatForm->addRow(QObject::tr("心跳指令:"), heartbeatInstructionEdit);

    auto* heartbeatActionRow = new QHBoxLayout();
    auto* heartbeatApplyBtn = new QPushButton(QObject::tr("保存心跳配置"), heartbeatGroup);
    auto* heartbeatTriggerBtn = new QPushButton(QObject::tr("立即触发"), heartbeatGroup);
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

    auto* stateGroup = new QGroupBox(QObject::tr("心跳实时状态"), statePage);
    stateGroup->setProperty("class", "SettingsGroup");
    auto* stateForm = new QFormLayout(stateGroup);
    stateForm->setContentsMargins(15, 20, 15, 15);
    stateForm->setVerticalSpacing(10);

    auto* heartbeatStatePathLabel = new QLabel(stateGroup);
    heartbeatStatePathLabel->setProperty("class", "PathLabel");
    stateForm->addRow(QObject::tr("状态存储:"), heartbeatStatePathLabel);

    auto* heartbeatLastSnapshotLabel = new QLabel(QStringLiteral("—"), stateGroup);
    stateForm->addRow(QObject::tr("上次巡检时间:"), heartbeatLastSnapshotLabel);
    auto* heartbeatLastNotifyLabel = new QLabel(QStringLiteral("—"), stateGroup);
    stateForm->addRow(QObject::tr("上次通知时间:"), heartbeatLastNotifyLabel);
    auto* heartbeatLastChangeLabel = new QLabel(QStringLiteral("—"), stateGroup);
    stateForm->addRow(QObject::tr("上次变化时间:"), heartbeatLastChangeLabel);
    auto* heartbeatReasonLabel = new QLabel(QStringLiteral("—"), stateGroup);
    heartbeatReasonLabel->setWordWrap(true);
    stateForm->addRow(QObject::tr("变化原因:"), heartbeatReasonLabel);
    auto* heartbeatJobsLabel = new QLabel(QStringLiteral("0"), stateGroup);
    stateForm->addRow(QObject::tr("活跃任务数:"), heartbeatJobsLabel);
    auto* heartbeatProviderDownLabel = new QLabel(QStringLiteral("否"), stateGroup);
    stateForm->addRow(QObject::tr("Provider 离线:"), heartbeatProviderDownLabel);
    auto* heartbeatWatchSignalsLabel = new QLabel(QStringLiteral("—"), stateGroup);
    heartbeatWatchSignalsLabel->setWordWrap(true);
    stateForm->addRow(QObject::tr("监视信号:"), heartbeatWatchSignalsLabel);
    auto* heartbeatDigestLabel = new QLabel(QStringLiteral("—"), stateGroup);
    heartbeatDigestLabel->setProperty("class", "MonospaceLabel");
    heartbeatDigestLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    stateForm->addRow(QObject::tr("快照摘要:"), heartbeatDigestLabel);
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

        const QJsonObject policyObj = capabilities.memory.loadPolicyObject ? capabilities.memory.loadPolicyObject(nullptr) : QJsonObject();
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

        const QStringList configIds = capabilities.modelCatalog
            ? capabilities.modelCatalog->registeredModelConfigIds()
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
        if (stewardConfigId.isEmpty() && capabilities.governanceQueries)
            stewardConfigId = ModelFactory::resolveConfigKey(capabilities.governanceQueries->defaultAgentConfig());
        int modelIndex = memoryStewardModelCombo->findData(stewardConfigId);
        if (modelIndex < 0 && memoryStewardModelCombo->count() > 0)
            modelIndex = 0;
        if (modelIndex >= 0)
            memoryStewardModelCombo->setCurrentIndex(modelIndex);

        const QString userMd = capabilities.memory.loadUserMarkdown ? capabilities.memory.loadUserMarkdown(nullptr) : QString();
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
        if (!capabilities.memoryCommands || !capabilities.governanceCommands || !capabilities.persistence) {
            if (error)
                *error = QObject::tr("配置服务不可用");
            return false;
        }

        const QString stewardId = memoryStewardCombo->currentData().toString().trimmed();
        const QString stewardModelId = memoryStewardModelCombo->currentData().toString().trimmed();

        QJsonObject policyObj = capabilities.memory.loadPolicyObject ? capabilities.memory.loadPolicyObject(nullptr) : QJsonObject();
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
        if (!capabilities.memory.savePolicyObject || !capabilities.memory.savePolicyObject(policyObj)) {
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
        if (!capabilities.memory.saveUserMarkdown || !capabilities.memory.saveUserMarkdown(userMarkdown, &userSaveError)) {
            if (error)
                *error = userSaveError.isEmpty() ? QObject::tr("写入 user.md 失败") : userSaveError;
            return false;
        }

        capabilities.governanceCommands->applyConfigToAllRuntimes();
        capabilities.persistence->saveSessionsToDisk();
        return true;
    };

    auto onMemoryStewardChanged = [&]() {
        if (memoryUiLoading)
            return;
        const QString stewardId = memoryStewardCombo->currentData().toString().trimmed();
        if (stewardId.isEmpty() || !capabilities.governanceQueries)
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
            heartbeatProviderDownLabel->setText(QStringLiteral("否"));
            heartbeatWatchSignalsLabel->setText(QStringLiteral("—"));
            heartbeatDigestLabel->setText(QStringLiteral("—"));
            return;
        }

        const QString statePath = capabilities.heartbeat.runtimeStateLocation ? capabilities.heartbeat.runtimeStateLocation(agentId) : QString();
        heartbeatStatePathLabel->setText(statePath);
        bool ok = false;
        const QJsonObject state = capabilities.heartbeat.loadRuntimeState ? capabilities.heartbeat.loadRuntimeState(agentId, &ok) : QJsonObject();
        if (!ok || state.isEmpty()) {
            const QString pending = QObject::tr("暂无（等待首次心跳）");
            heartbeatLastSnapshotLabel->setText(pending);
            heartbeatLastNotifyLabel->setText(pending);
            heartbeatLastChangeLabel->setText(pending);
            heartbeatReasonLabel->setText(QStringLiteral("—"));
            heartbeatJobsLabel->setText(QStringLiteral("0"));
            heartbeatProviderDownLabel->setText(QStringLiteral("否"));
            QStringList configuredSignals;
            configuredSignals = normalizeHeartbeatSignalNames(capabilities.heartbeat.configForAgent ? capabilities.heartbeat.configForAgent(agentId).snapshotSignals : QStringList());
            heartbeatWatchSignalsLabel->setText(
                configuredSignals.isEmpty() ? QObject::tr("默认(provider/delegate/pulse)") : configuredSignals.join(QStringLiteral(", ")));
            heartbeatDigestLabel->setText(QStringLiteral("—"));
            return;
        }

        heartbeatLastSnapshotLabel->setText(utcFieldToLocalText(state, QStringLiteral("last_snapshot_at_utc")));
        heartbeatLastNotifyLabel->setText(utcFieldToLocalText(state, QStringLiteral("last_notify_at_utc")));
        heartbeatLastChangeLabel->setText(utcFieldToLocalText(state, QStringLiteral("last_change_at_utc")));
        const QString reason = state.value(QStringLiteral("last_reason")).toString().trimmed();
        heartbeatReasonLabel->setText(reason.isEmpty() ? QStringLiteral("—") : reason);
        heartbeatJobsLabel->setText(QString::number(state.value(QStringLiteral("active_jobs_count")).toInt(0)));
        heartbeatProviderDownLabel->setText(state.value(QStringLiteral("provider_down")).toBool(false) ? QObject::tr("是") : QObject::tr("否"));
        QStringList ws;
        const QJsonArray wsa = state.value(QStringLiteral("watch_signals")).toArray();
        for (const QJsonValue& v : wsa)
            ws.append(normalizeHeartbeatSignalName(v.toString()));
        heartbeatWatchSignalsLabel->setText(ws.join(QStringLiteral(", ")));
        const QString digest = state.value(QStringLiteral("last_snapshot_digest")).toString().trimmed();
        heartbeatDigestLabel->setText(digest.isEmpty() ? QStringLiteral("—") : digest.left(32) + QStringLiteral("..."));
        heartbeatDigestLabel->setToolTip(digest);
    };

    auto loadHeartbeatUiForSelected = [=]() {
        const QString agentId = heartbeatAgentCombo->currentData().toString().trimmed();
        if (agentId.isEmpty()) {
            heartbeatEnabledCheck->setChecked(true);
            heartbeatIntervalSpin->setValue(30 * 60);
            heartbeatSilentNoChangeCheck->setChecked(true);
            heartbeatNotifyOnChangeOnlyCheck->setChecked(true);
            heartbeatNotifyIntervalSpin->setValue(30);
            heartbeatPersistNoChangeCheck->setChecked(false);
            heartbeatStatePersistIntervalSpin->setValue(60);
            heartbeatStartEdit->setText(QStringLiteral("08:00"));
            heartbeatEndEdit->setText(QStringLiteral("23:00"));
            heartbeatTimezoneEdit->setText(QStringLiteral("Asia/Shanghai"));
            heartbeatSignalProviderCheck->setChecked(true);
            heartbeatSignalDelegateCheck->setChecked(true);
            heartbeatSignalPulseCheck->setChecked(true);
            heartbeatSignalSchedulerCheck->setChecked(false);
            heartbeatSignalMemoryCheck->setChecked(false);
            heartbeatSignalExtraEdit->clear();
            heartbeatInstructionEdit->setPlainText(QString());
            heartbeatInstructionEdit->setProperty("heartbeatPath", QString());
            heartbeatPathLabel->setText(QObject::tr("未选择助手"));
            refreshHeartbeatStateUiForSelected();
            return;
        }

        const HeartbeatConfig cfg = capabilities.heartbeat.configForAgent ? capabilities.heartbeat.configForAgent(agentId) : HeartbeatConfig {};
        heartbeatEnabledCheck->setChecked(cfg.enabled);
        heartbeatIntervalSpin->setValue(qMax(5, cfg.intervalMs / 1000));
        heartbeatSilentNoChangeCheck->setChecked(cfg.silentWhenNoChange);
        heartbeatNotifyOnChangeOnlyCheck->setChecked(cfg.notifyOnChangeOnly);
        heartbeatNotifyIntervalSpin->setValue(qMax(1, cfg.notifyMinIntervalMs / (60 * 1000)));
        heartbeatPersistNoChangeCheck->setChecked(cfg.persistStateOnNoChange);
        heartbeatStatePersistIntervalSpin->setValue(qMax(1, cfg.statePersistIntervalMs / 1000));
        heartbeatStartEdit->setText(cfg.activeHours.start.isValid() ? cfg.activeHours.start.toString(QStringLiteral("HH:mm")) : QStringLiteral("08:00"));
        heartbeatEndEdit->setText(cfg.activeHours.end.isValid() ? cfg.activeHours.end.toString(QStringLiteral("HH:mm")) : QStringLiteral("23:00"));
        heartbeatTimezoneEdit->setText(cfg.activeHours.timezone.trimmed().isEmpty() ? QStringLiteral("Asia/Shanghai") : cfg.activeHours.timezone.trimmed());

        const QStringList signalNames = normalizeHeartbeatSignalNames(cfg.snapshotSignals);
        heartbeatSignalProviderCheck->setChecked(signalNames.contains(QStringLiteral("provider_status")));
        heartbeatSignalDelegateCheck->setChecked(signalNames.contains(QStringLiteral("delegate_jobs")));
        heartbeatSignalPulseCheck->setChecked(signalNames.contains(QStringLiteral("pulse_state")));
        heartbeatSignalSchedulerCheck->setChecked(signalNames.contains(QStringLiteral("scheduler_jobs")));
        heartbeatSignalMemoryCheck->setChecked(signalNames.contains(QStringLiteral("memory_progress")));

        QStringList extra;
        for (const QString& s : signalNames) {
            if (s != QLatin1String("provider_status")
                && s != QLatin1String("delegate_jobs")
                && s != QLatin1String("pulse_state")
                && s != QLatin1String("scheduler_jobs")
                && s != QLatin1String("memory_progress")) {
                extra.append(s);
            }
        }
        heartbeatSignalExtraEdit->setText(extra.join(QStringLiteral(", ")));

        QString path = cfg.heartbeatPath.trimmed();
        if (path.isEmpty())
            path = capabilities.heartbeat.pathForAgent ? capabilities.heartbeat.pathForAgent(agentId).trimmed() : QString();
        if (path.isEmpty())
            path = capabilities.heartbeat.instructionPath ? capabilities.heartbeat.instructionPath(agentId) : QString();
        heartbeatInstructionEdit->setProperty("heartbeatPath", path);
        heartbeatPathLabel->setText(path);
        heartbeatInstructionEdit->setPlainText(capabilities.heartbeat.readInstructionText ? capabilities.heartbeat.readInstructionText(path, nullptr) : QString());
        refreshHeartbeatStateUiForSelected();
    };

    auto applyHeartbeatUiForSelected = [=](bool showToast) -> bool {
        const QString agentId = heartbeatAgentCombo->currentData().toString().trimmed();
        if (agentId.isEmpty())
            return true;

        HeartbeatConfig cfg = capabilities.heartbeat.configForAgent ? capabilities.heartbeat.configForAgent(agentId) : HeartbeatConfig {};
        cfg.enabled = heartbeatEnabledCheck->isChecked();
        cfg.intervalMs = qMax(1000, heartbeatIntervalSpin->value() * 1000);
        cfg.silentWhenNoChange = heartbeatSilentNoChangeCheck->isChecked();
        cfg.notifyOnChangeOnly = heartbeatNotifyOnChangeOnlyCheck->isChecked();
        cfg.notifyMinIntervalMs = qMax(1000, heartbeatNotifyIntervalSpin->value() * 60 * 1000);
        cfg.persistStateOnNoChange = heartbeatPersistNoChangeCheck->isChecked();
        cfg.statePersistIntervalMs = qMax(1000, heartbeatStatePersistIntervalSpin->value() * 1000);

        QStringList selected;
        if (heartbeatSignalProviderCheck->isChecked())
            selected << QStringLiteral("provider_status");
        if (heartbeatSignalDelegateCheck->isChecked())
            selected << QStringLiteral("delegate_jobs");
        if (heartbeatSignalPulseCheck->isChecked())
            selected << QStringLiteral("pulse_state");
        if (heartbeatSignalSchedulerCheck->isChecked())
            selected << QStringLiteral("scheduler_jobs");
        if (heartbeatSignalMemoryCheck->isChecked())
            selected << QStringLiteral("memory_progress");
        selected << parseHeartbeatSignalInput(heartbeatSignalExtraEdit->text());
        cfg.snapshotSignals = normalizeHeartbeatSignalNames(selected);

        const QTime startParsed = QTime::fromString(heartbeatStartEdit->text().trimmed(), QStringLiteral("HH:mm"));
        const QTime endParsed = QTime::fromString(heartbeatEndEdit->text().trimmed(), QStringLiteral("HH:mm"));
        cfg.activeHours.start = startParsed.isValid() ? startParsed : QTime(8, 0);
        cfg.activeHours.end = endParsed.isValid() ? endParsed : QTime(23, 0);
        cfg.activeHours.timezone = heartbeatTimezoneEdit->text().trimmed();
        if (cfg.activeHours.timezone.isEmpty())
            cfg.activeHours.timezone = QStringLiteral("Asia/Shanghai");

        QString path = heartbeatInstructionEdit->property("heartbeatPath").toString().trimmed();
        if (path.isEmpty())
            path = cfg.heartbeatPath.trimmed();
        if (path.isEmpty())
            path = capabilities.heartbeat.instructionPath ? capabilities.heartbeat.instructionPath(agentId) : QString();
        cfg.heartbeatPath = path;
        heartbeatPathLabel->setText(path);

        QString heartbeatWriteError;
        if (!capabilities.heartbeat.writeInstructionText
            || !capabilities.heartbeat.writeInstructionText(path, heartbeatInstructionEdit->toPlainText(), &heartbeatWriteError)) {
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

        if (capabilities.heartbeat.updateConfig)
            capabilities.heartbeat.updateConfig(agentId, cfg);
        if (capabilities.heartbeat.startForAgent)
            capabilities.heartbeat.startForAgent(agentId);
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
        if (!capabilities.scheduler.jobById || !capabilities.scheduler.jobById(jobId, &job))
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

        QList<ScheduledJob> jobs = capabilities.scheduler.allJobs ? capabilities.scheduler.allJobs() : QList<ScheduledJob>();
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
        const bool ok = capabilities.memoryCommands->rebuildMemoryIndexAs(userId, QString(), &rebuildResult, &rebuildError);
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
        if (capabilities.heartbeat.triggerForAgent)
            capabilities.heartbeat.triggerForAgent(agentId, QStringLiteral("manual_ui"));
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
            const QString newId = capabilities.scheduler.addJob ? capabilities.scheduler.addJob(job) : QString();
            if (newId.trimmed().isEmpty()) {
                QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("创建任务失败。"));
                return;
            }
            reloadSchedulerJobs(newId);
        } else {
            if (!capabilities.scheduler.updateJob || !capabilities.scheduler.updateJob(jobId, job)) {
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
        if (!capabilities.scheduler.removeJob || !capabilities.scheduler.removeJob(jobId)) {
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
        if (capabilities.scheduler.triggerJob)
            capabilities.scheduler.triggerJob(jobId);
        QMessageBox::information(parent, QObject::tr("已触发"), QObject::tr("已触发定时任务。"));
    });

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [=, &dlg]() {
        QString err;
        if (!saveMemorySettingsUi(&err)) {
            QMessageBox::warning(parent, QObject::tr("保存失败"), err.isEmpty() ? QObject::tr("信息设置保存失败。") : err);
            return;
        }
        if (!applyHeartbeatUiForSelected(false)) {
            QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("心跳配置保存失败。"));
            return;
        }
        QMessageBox::information(parent, QObject::tr("保存成功"), QObject::tr("信息设置已更新。"));
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    reloadMemorySettingsUi();
    loadHeartbeatUiForSelected();
    auto* heartbeatStateTimer = new QTimer(&dlg);
    heartbeatStateTimer->setInterval(3000);
    QObject::connect(heartbeatStateTimer, &QTimer::timeout, &dlg, [=]() { refreshHeartbeatStateUiForSelected(); });
    heartbeatStateTimer->start();
    reloadSchedulerJobs(QString());

    if (!canManageGlobalConfig) {
        heartbeatGroup->setEnabled(false);
        schedulerGroup->setEnabled(false);
    }

    dlg.exec();
}

} // namespace InformationSettingsDialog
