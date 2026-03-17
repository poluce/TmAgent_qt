#include "ExecutionRecordWindow.h"

#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QLabel* makeLabel(const QString& text, const char* style, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(QString::fromUtf8(style));
    return label;
}

QLabel* makeTag(const QString& text, QWidget* parent, const QString& background = QStringLiteral("#e2e8f0"), const QString& color = QStringLiteral("#475569"))
{
    QLabel* label = new QLabel(text, parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(
        QStringLiteral("QLabel { background:%1; color:%2; border-radius:999px; padding:4px 10px; font-size:11px; font-weight:700; }")
            .arg(background, color));
    return label;
}

QFrame* makeCard(QWidget* parent, const char* style)
{
    QFrame* frame = new QFrame(parent);
    frame->setStyleSheet(QString::fromUtf8(style));
    return frame;
}

QPlainTextEdit* makeCodeView(QWidget* parent)
{
    QPlainTextEdit* edit = new QPlainTextEdit(parent);
    edit->setReadOnly(true);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit->setMinimumHeight(250);
    edit->setStyleSheet("QPlainTextEdit { background:#0f172a; color:#dbeafe; border:1px solid #1e3a8a; border-radius:14px; padding:10px; font:12px Consolas; }");
    return edit;
}

QColor toneColor(const QString& tone)
{
    if (tone == QLatin1String("error"))
        return QColor("#dc2626");
    if (tone == QLatin1String("warning"))
        return QColor("#b45309");
    if (tone == QLatin1String("info"))
        return QColor("#2563eb");
    if (tone == QLatin1String("success"))
        return QColor("#059669");
    return QColor("#475569");
}

QString firstNonEmpty(const QStringList& values)
{
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return QString();
}

QString finishReasonOf(const QJsonObject& response)
{
    const QJsonArray choices = response.value("choices").toArray();
    if (!choices.isEmpty()) {
        const QString reason = choices.first().toObject().value("finish_reason").toString().trimmed();
        if (!reason.isEmpty())
            return reason;
    }
    return response.value("finish_reason").toString().trimmed();
}

void clearLayout(QLayout* layout)
{
    while (layout && layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (item->widget())
            delete item->widget();
        if (item->layout()) {
            clearLayout(item->layout());
            delete item->layout();
        }
        delete item;
    }
}
} // namespace

ExecutionRecordWindow::ExecutionRecordWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlag(Qt::Window, true);
    setupUi();
}

void ExecutionRecordWindow::setupUi()
{
    setStyleSheet("QWidget { font-family:'Microsoft YaHei'; }");
    setWindowTitle(QStringLiteral("执行记录工作台"));
    resize(1460, 920);
    setMinimumSize(1260, 820);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(18);

    QFrame* heroLeft = makeCard(this, "QFrame { background:#ffffff; border:1px solid #dbe4f0; border-radius:22px; }");
    auto* heroLeftLayout = new QVBoxLayout(heroLeft);
    heroLeftLayout->setContentsMargins(22, 22, 22, 22);
    heroLeftLayout->setSpacing(10);
    heroLeftLayout->addWidget(makeLabel(QStringLiteral("Execution Record V2"),
                                        "color:#2563eb; font-size:12px; font-weight:700;",
                                        heroLeft));
    m_titleLabel = makeLabel(QStringLiteral("执行记录工作台"),
                             "color:#0f172a; font-size:30px; font-weight:800;",
                             heroLeft);
    heroLeftLayout->addWidget(m_titleLabel);
    m_sessionLabel = makeLabel(QStringLiteral("当前会话"),
                               "color:#475569; font-size:13px;",
                               heroLeft);
    heroLeftLayout->addWidget(m_sessionLabel);
    m_introLabel = makeLabel(QStringLiteral("先看结论，再看过程，最后核对证据。这个窗口不再只是右边的小详情栏，而是完整的执行工作台。"),
                             "color:#475569; font-size:13px; line-height:1.6;",
                             heroLeft);
    heroLeftLayout->addWidget(m_introLabel);
    auto* chipRow = new QHBoxLayout();
    const QStringList chips = { QStringLiteral("先看结论"), QStringLiteral("再看过程"), QStringLiteral("最后核对证据"),
                                QStringLiteral("错误自动聚焦"), QStringLiteral("行动建议清晰") };
    for (const QString& text : chips) {
        chipRow->addWidget(makeLabel(text,
                                     "background:#eff6ff; color:#1d4ed8; border:1px solid #bfdbfe; border-radius:999px; padding:6px 12px; font-size:12px; font-weight:700;",
                                     heroLeft));
    }
    chipRow->addStretch();
    heroLeftLayout->addLayout(chipRow);

    QFrame* heroRight = makeCard(this, "QFrame { background:#ffffff; border:1px solid #dbe4f0; border-radius:22px; }");
    auto* heroRightLayout = new QVBoxLayout(heroRight);
    heroRightLayout->setContentsMargins(22, 22, 22, 22);
    heroRightLayout->setSpacing(12);
    heroRightLayout->addWidget(makeTag(QStringLiteral("场景判断"), heroRight, QStringLiteral("#dbeafe"), QStringLiteral("#2563eb")), 0, Qt::AlignLeft);
    m_sceneTitleLabel = makeLabel(QStringLiteral("等待选择记录"),
                                  "color:#0f172a; font-size:22px; font-weight:800;",
                                  heroRight);
    heroRightLayout->addWidget(m_sceneTitleLabel);
    auto* sceneRow = new QHBoxLayout();
    m_sceneSuccessChip = makeLabel(QStringLiteral("成功完成"), "padding:8px 10px;", heroRight);
    m_sceneWaitingChip = makeLabel(QStringLiteral("工具执行中"), "padding:8px 10px;", heroRight);
    m_sceneFailureChip = makeLabel(QStringLiteral("失败阻塞"), "padding:8px 10px;", heroRight);
    sceneRow->addWidget(m_sceneSuccessChip);
    sceneRow->addWidget(m_sceneWaitingChip);
    sceneRow->addWidget(m_sceneFailureChip);
    sceneRow->addStretch();
    heroRightLayout->addLayout(sceneRow);
    m_sceneHelperLabel = makeLabel(QStringLiteral("选择左侧任意一条记录后，这里会告诉你当前是成功、处理中还是失败阻塞。"),
                                   "color:#475569; font-size:13px; line-height:1.6;",
                                   heroRight);
    heroRightLayout->addWidget(m_sceneHelperLabel);
    heroRightLayout->addStretch();

    auto* heroRow = new QHBoxLayout();
    heroRow->setSpacing(18);
    heroRow->addWidget(heroLeft, 11);
    heroRow->addWidget(heroRight, 9);
    root->addLayout(heroRow);

    auto* body = new QHBoxLayout();
    body->setSpacing(18);

    QFrame* leftPanel = makeCard(this, "QFrame { background:#ffffff; border:1px solid #dbe4f0; border-radius:20px; }");
    leftPanel->setFixedWidth(320);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(18, 18, 18, 18);
    leftLayout->setSpacing(12);
    auto* leftHead = new QHBoxLayout();
    leftHead->addWidget(makeLabel(QStringLiteral("轮次列表"), "color:#0f172a; font-size:18px; font-weight:700;", leftPanel), 1);
    m_turnCountBadge = makeLabel(QStringLiteral("共 0 条"),
                                 "border:1px solid #bfdbfe; background:#eff6ff; color:#1d4ed8; border-radius:999px; padding:6px 10px; font-size:12px; font-weight:700;",
                                 leftPanel);
    leftHead->addWidget(m_turnCountBadge, 0, Qt::AlignTop);
    leftLayout->addLayout(leftHead);
    auto* filterRow = new QHBoxLayout();
    m_filterCombo = new QComboBox(leftPanel);
    m_recentCombo = new QComboBox(leftPanel);
    m_filterCombo->setStyleSheet("QComboBox { background:#fff; border:1px solid #dbe4f0; border-radius:10px; padding:6px 10px; }");
    m_recentCombo->setStyleSheet("QComboBox { background:#fff; border:1px solid #dbe4f0; border-radius:10px; padding:6px 10px; }");
    filterRow->addWidget(m_filterCombo, 1);
    filterRow->addWidget(m_recentCombo);
    leftLayout->addLayout(filterRow);
    m_clearHistoryBtn = new QPushButton(QStringLiteral("清空历史"), leftPanel);
    m_clearHistoryBtn->setStyleSheet("QPushButton { background:#fff; border:1px solid #dbe4f0; border-radius:10px; padding:7px 12px; color:#334155; font-weight:700; }");
    leftLayout->addWidget(m_clearHistoryBtn, 0, Qt::AlignLeft);
    m_turnList = new QListWidget(leftPanel);
    m_turnList->setSpacing(10);
    m_turnList->setFrameShape(QFrame::NoFrame);
    leftLayout->addWidget(m_turnList, 1);
    body->addWidget(leftPanel, 0);

    QFrame* centerPanel = makeCard(this, "QFrame { background:#ffffff; border:1px solid #dbe4f0; border-radius:20px; }");
    auto* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(18, 18, 18, 18);
    centerLayout->setSpacing(14);
    auto* centerHead = new QHBoxLayout();
    centerHead->addWidget(makeLabel(QStringLiteral("执行驾驶舱"), "color:#0f172a; font-size:18px; font-weight:700;", centerPanel), 1);
    m_historySummaryStatusBadge = new QLabel(centerPanel);
    centerHead->addWidget(m_historySummaryStatusBadge, 0, Qt::AlignTop);
    centerLayout->addLayout(centerHead);

    QFrame* overview = makeCard(centerPanel, "QFrame { background:#ffffff; border:1px solid #e2e8f0; border-radius:18px; }");
    auto* overviewLayout = new QVBoxLayout(overview);
    overviewLayout->setContentsMargins(16, 16, 16, 16);
    overviewLayout->setSpacing(12);
    auto addSummaryBlock = [overview](const QString& title, QLabel*& target) {
        QFrame* block = makeCard(overview, "QFrame { background:#f8fafc; border:none; border-radius:16px; }");
        auto* blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(12, 12, 12, 12);
        blockLayout->setSpacing(6);
        blockLayout->addWidget(makeLabel(title,
                                         "background:#e2e8f0; color:#475569; border-radius:999px; padding:4px 10px; font-size:11px; font-weight:700;",
                                         block));
        target = makeLabel(QString(), "color:#0f172a; font-size:13px; font-weight:700; line-height:1.55;", block);
        blockLayout->addWidget(target);
        return block;
    };
    auto* metaRow = new QHBoxLayout();
    metaRow->setSpacing(10);
    metaRow->addWidget(addSummaryBlock(QStringLiteral("记录类型"), m_historySummaryTypeValue), 2);
    metaRow->addWidget(addSummaryBlock(QStringLiteral("时间"), m_historySummaryTimeValue), 3);
    metaRow->addWidget(addSummaryBlock(QStringLiteral("关键信息"), m_historySummaryMetaValue), 4);
    overviewLayout->addLayout(metaRow);

    auto addMetric = [overview](QGridLayout* grid, int column, const QString& title, QLabel*& target) {
        QFrame* card = makeCard(overview, "QFrame { background:#f8fafc; border:1px solid #e2e8f0; border-radius:16px; }");
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(6);
        layout->addWidget(makeTag(title, card), 0, Qt::AlignLeft);
        target = makeLabel(QString(), "color:#0f172a; font-size:22px; font-weight:800;", card);
        layout->addWidget(target);
        grid->addWidget(card, 0, column);
    };
    auto* metricGrid = new QGridLayout();
    addMetric(metricGrid, 0, QStringLiteral("总状态"), m_metricStatusValue);
    addMetric(metricGrid, 1, QStringLiteral("总耗时"), m_metricDurationValue);
    addMetric(metricGrid, 2, QStringLiteral("工具调用"), m_metricToolCountValue);
    addMetric(metricGrid, 3, QStringLiteral("异常次数"), m_metricExceptionValue);
    addMetric(metricGrid, 4, QStringLiteral("当前阶段"), m_metricStageValue);
    overviewLayout->addLayout(metricGrid);
    centerLayout->addWidget(overview);

    auto addDetailCard = [centerPanel](QHBoxLayout* row, const QString& title, QLabel*& a, QLabel*& b, QLabel*& c,
                                       const QString& t1, const QString& t2, const QString& t3) {
        QFrame* card = makeCard(centerPanel, "QFrame { background:#ffffff; border:1px solid #e2e8f0; border-radius:18px; }");
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);
        layout->addWidget(makeTag(title, card, QStringLiteral("#dbeafe"), QStringLiteral("#1d4ed8")), 0, Qt::AlignLeft);
        auto addTile = [card](const QString& label, QLabel*& target, const char* valueStyle) {
            QFrame* box = makeCard(card, "QFrame { background:#f8fafc; border:none; border-radius:14px; }");
            auto* boxLayout = new QVBoxLayout(box);
            boxLayout->setContentsMargins(12, 12, 12, 12);
            boxLayout->setSpacing(6);
            boxLayout->addWidget(makeTag(label, box), 0, Qt::AlignLeft);
            target = makeLabel(QString(), valueStyle, box);
            boxLayout->addWidget(target);
            return box;
        };

        QFrame* hero = addTile(t1, a, "color:#0f172a; font-size:15px; font-weight:700; line-height:1.6;");
        hero->setStyleSheet("QFrame { background:#eff6ff; border:none; border-radius:16px; }");
        layout->addWidget(hero);

        auto* bottomGrid = new QGridLayout();
        bottomGrid->setHorizontalSpacing(10);
        bottomGrid->setVerticalSpacing(10);
        bottomGrid->addWidget(addTile(t2, b, "color:#0f172a; font-size:13px; font-weight:600; line-height:1.5;"), 0, 0);
        bottomGrid->addWidget(addTile(t3, c, "color:#0f172a; font-size:13px; font-weight:600; line-height:1.5;"), 0, 1);
        layout->addLayout(bottomGrid);
        row->addWidget(card, 1);
    };
    auto* summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(14);
    addDetailCard(summaryRow, QStringLiteral("本轮结论"), m_decisionConclusionValue, m_decisionBlockerValue, m_decisionNextValue,
                  QStringLiteral("结论"), QStringLiteral("阻塞点"), QStringLiteral("下一步"));
    addDetailCard(summaryRow, QStringLiteral("问题定位"), m_diagnosisRootCauseValue, m_diagnosisReliabilityValue, m_diagnosisActionValue,
                  QStringLiteral("根因判断"), QStringLiteral("可信度"), QStringLiteral("建议动作"));
    centerLayout->addLayout(summaryRow);

    auto addFlowPage = [centerPanel](const QString& title, const QString& desc, QVBoxLayout*& targetLayout) {
        QWidget* page = new QWidget(centerPanel);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(10);
        pageLayout->addWidget(makeTag(title, page, QStringLiteral("#dbeafe"), QStringLiteral("#1d4ed8")), 0, Qt::AlignLeft);
        pageLayout->addWidget(makeLabel(desc, "color:#64748b; font-size:12px; line-height:1.6;", page));
        QScrollArea* area = new QScrollArea(page);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setStyleSheet("QScrollArea { background:transparent; }");
        QWidget* box = new QWidget(area);
        targetLayout = new QVBoxLayout(box);
        targetLayout->setContentsMargins(0, 0, 0, 0);
        targetLayout->setSpacing(10);
        area->setWidget(box);
        pageLayout->addWidget(area, 1);
        return page;
    };
    QFrame* workspaceCard = makeCard(centerPanel, "QFrame { background:#ffffff; border:1px solid #e2e8f0; border-radius:18px; }");
    auto* workspaceLayout = new QVBoxLayout(workspaceCard);
    workspaceLayout->setContentsMargins(16, 16, 16, 16);
    workspaceLayout->setSpacing(12);
    workspaceLayout->addWidget(makeTag(QStringLiteral("执行工作区"), workspaceCard, QStringLiteral("#dbeafe"), QStringLiteral("#1d4ed8")), 0, Qt::AlignLeft);
    QTabWidget* workspaceTabs = new QTabWidget(workspaceCard);
    workspaceTabs->setStyleSheet(
        "QTabWidget::pane { border:none; top:8px; }"
        "QTabBar::tab { background:#fff; border:1px solid #dbe4f0; border-radius:10px; padding:8px 12px; margin-right:6px; color:#64748b; font-weight:700; }"
        "QTabBar::tab:selected { background:#eff6ff; border-color:#93c5fd; color:#1d4ed8; }");
    workspaceTabs->addTab(addFlowPage(QStringLiteral("执行时间线"),
                                      QStringLiteral("只保留关键节点，不再把所有字段顺着往下排。"),
                                      m_timelineLayout),
                          QStringLiteral("时间线"));
    workspaceTabs->addTab(addFlowPage(QStringLiteral("工具过程"),
                                      QStringLiteral("按步骤看工具链，不再和时间线并排挤在一起。"),
                                      m_toolLayout),
                          QStringLiteral("工具过程"));

    QWidget* evidencePage = new QWidget(workspaceTabs);
    auto* evidencePageLayout = new QVBoxLayout(evidencePage);
    evidencePageLayout->setContentsMargins(0, 0, 0, 0);
    evidencePageLayout->setSpacing(10);
    evidencePageLayout->addWidget(makeTag(QStringLiteral("证据抽屉"), evidencePage, QStringLiteral("#dbeafe"), QStringLiteral("#1d4ed8")), 0, Qt::AlignLeft);
    m_historyRawHintLabel = makeLabel(HistoryFormatters::rawTabHintText(),
                                      "background:#fff7ed; color:#9a3412; border:1px solid #fdba74; border-radius:14px; padding:8px 10px; font-size:12px;",
                                      evidencePage);
    evidencePageLayout->addWidget(m_historyRawHintLabel);
    m_evidenceTabs = new QTabWidget(evidencePage);
    m_evidenceTabs->setStyleSheet(
        "QTabWidget::pane { border:none; top:8px; }"
        "QTabBar::tab { background:#fff; border:1px solid #dbe4f0; border-radius:10px; padding:8px 12px; margin-right:6px; color:#64748b; font-weight:700; }"
        "QTabBar::tab:selected { background:#eff6ff; border-color:#93c5fd; color:#1d4ed8; }");
    m_summaryLayerView = makeCodeView(m_evidenceTabs);
    m_eventLayerView = makeCodeView(m_evidenceTabs);
    m_interactionLayerView = makeCodeView(m_evidenceTabs);
    m_auditLayerView = makeCodeView(m_evidenceTabs);
    m_evidenceTabs->addTab(m_summaryLayerView, QStringLiteral("展示摘要层"));
    m_evidenceTabs->addTab(m_eventLayerView, QStringLiteral("事件层"));
    m_evidenceTabs->addTab(m_interactionLayerView, QStringLiteral("交互事实层"));
    m_evidenceTabs->addTab(m_auditLayerView, QStringLiteral("审计层（未建设）"));
    evidencePageLayout->addWidget(m_evidenceTabs, 1);
    workspaceTabs->addTab(evidencePage, QStringLiteral("证据抽屉"));
    workspaceLayout->addWidget(workspaceTabs, 1);
    centerLayout->addWidget(workspaceCard, 1);
    body->addWidget(centerPanel, 1);

    QFrame* rightPanel = makeCard(this, "QFrame { background:#ffffff; border:1px solid #dbe4f0; border-radius:20px; }");
    rightPanel->setFixedWidth(300);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(18, 18, 18, 18);
    rightLayout->setSpacing(12);
    auto addInsightGrid = [rightPanel](const QString& title,
                                       QLabel*& a,
                                       QLabel*& b,
                                       QLabel*& c,
                                       QLabel*& d,
                                       const QString& t1,
                                       const QString& t2,
                                       const QString& t3,
                                       const QString& t4) {
        QWidget* page = new QWidget(rightPanel);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(10);
        pageLayout->addWidget(makeTag(title, page, QStringLiteral("#dbeafe"), QStringLiteral("#1d4ed8")), 0, Qt::AlignLeft);

        auto addText = [page](QGridLayout* layout, int r, int c, const QString& label, QLabel*& target) {
            QFrame* box = makeCard(page, "QFrame { background:#f8fafc; border:none; border-radius:14px; }");
            auto* boxLayout = new QVBoxLayout(box);
            boxLayout->setContentsMargins(12, 12, 12, 12);
            boxLayout->setSpacing(6);
            boxLayout->addWidget(makeTag(label, box), 0, Qt::AlignLeft);
            target = makeLabel(QString(), "color:#475569; font-size:12px; line-height:1.6;", box);
            boxLayout->addWidget(target);
            layout->addWidget(box, r, c);
        };

        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(10);
        addText(grid, 0, 0, t1, a);
        addText(grid, 0, 1, t2, b);
        addText(grid, 1, 0, t3, c);
        addText(grid, 1, 1, t4, d);
        pageLayout->addLayout(grid);
        pageLayout->addStretch();
        return page;
    };

    QTabWidget* sideTabs = new QTabWidget(rightPanel);
    sideTabs->setStyleSheet(
        "QTabWidget::pane { border:none; }"
        "QTabBar::tab { background:#fff; border:1px solid #dbe4f0; border-radius:10px; padding:7px 12px; margin-right:6px; color:#64748b; font-weight:700; }"
        "QTabBar::tab:selected { background:#eff6ff; border-color:#93c5fd; color:#1d4ed8; }");
    sideTabs->addTab(addInsightGrid(QStringLiteral("用户 5 秒判断"),
                                    m_quickResultValue,
                                    m_quickBlockerValue,
                                    m_quickCauseValue,
                                    m_quickActionValue,
                                    QStringLiteral("成功了吗"),
                                    QStringLiteral("卡在哪"),
                                    QStringLiteral("为什么"),
                                    QStringLiteral("现在怎么办")),
                     QStringLiteral("5 秒判断"));
    sideTabs->addTab(addInsightGrid(QStringLiteral("本轮摘录"),
                                    m_historySummaryInputValue,
                                    m_historySummaryOutputValue,
                                    m_historySummaryToolValue,
                                    m_historySummaryErrorValue,
                                    QStringLiteral("输入摘要"),
                                    QStringLiteral("输出摘要"),
                                    QStringLiteral("工具摘要"),
                                    QStringLiteral("错误 / 提醒")),
                     QStringLiteral("本轮摘录"));
    rightLayout->addWidget(sideTabs, 1);
    rightLayout->addWidget(makeLabel(QStringLiteral("先看顶部状态和结论，再看时间线与工具过程，最后打开证据抽屉核对原始层级。"),
                                     "color:#475569; font-size:12px; line-height:1.6;",
                                     rightPanel));
    rightLayout->addStretch();
    body->addWidget(rightPanel, 0);

    root->addLayout(body, 1);

    m_filterCombo->addItem(ExecutionHistory::filterModeText(ExecutionHistory::FilterMode::All), static_cast<int>(ExecutionHistory::FilterMode::All));
    m_filterCombo->addItem(ExecutionHistory::filterModeText(ExecutionHistory::FilterMode::FailuresOnly), static_cast<int>(ExecutionHistory::FilterMode::FailuresOnly));
    m_filterCombo->addItem(ExecutionHistory::filterModeText(ExecutionHistory::FilterMode::ToolCallsOnly), static_cast<int>(ExecutionHistory::FilterMode::ToolCallsOnly));
    m_filterCombo->addItem(ExecutionHistory::filterModeText(ExecutionHistory::FilterMode::EventsOnly), static_cast<int>(ExecutionHistory::FilterMode::EventsOnly));
    m_filterCombo->addItem(ExecutionHistory::filterModeText(ExecutionHistory::FilterMode::ActiveOnly), static_cast<int>(ExecutionHistory::FilterMode::ActiveOnly));
    m_recentCombo->addItem(QStringLiteral("全部"), 0);
    m_recentCombo->addItem(QStringLiteral("10 条"), 10);
    m_recentCombo->addItem(QStringLiteral("20 条"), 20);
    m_recentCombo->addItem(QStringLiteral("50 条"), 50);
    connect(m_turnList, &QListWidget::currentRowChanged, this, &ExecutionRecordWindow::onTurnSelectionChanged);
    connect(m_filterCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_syncingState)
            emit filterModeChanged(static_cast<ExecutionHistory::FilterMode>(m_filterCombo->currentData().toInt()));
    });
    connect(m_recentCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_syncingState)
            emit recentLimitChanged(m_recentCombo->currentData().toInt());
    });
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &ExecutionRecordWindow::clearHistoryRequested);
    resetRecord(false);
}

void ExecutionRecordWindow::setSessionTitle(const QString& title)
{
    const QString trimmed = title.trimmed();
    m_sessionLabel->setText(trimmed.isEmpty() ? QStringLiteral("当前会话") : QStringLiteral("当前会话：%1").arg(trimmed));
    setWindowTitle(trimmed.isEmpty() ? QStringLiteral("执行记录工作台") : QStringLiteral("执行记录工作台 - %1").arg(trimmed));
}

void ExecutionRecordWindow::setHistoryState(const QVector<ExecutionHistory::Record>& records,
                                            const QVector<int>& visibleIndexes,
                                            int currentVisibleRow,
                                            ExecutionHistory::FilterMode filterMode,
                                            int recentLimit)
{
    m_syncingState = true;
    m_records = records;
    m_visibleIndexes = visibleIndexes;
    m_turnCountBadge->setText(QStringLiteral("共 %1 条").arg(visibleIndexes.size()));

    {
        const QSignalBlocker blocker(m_filterCombo);
        const int index = m_filterCombo->findData(static_cast<int>(filterMode));
        if (index >= 0)
            m_filterCombo->setCurrentIndex(index);
    }
    {
        const QSignalBlocker blocker(m_recentCombo);
        const int index = m_recentCombo->findData(recentLimit);
        if (index >= 0)
            m_recentCombo->setCurrentIndex(index);
    }
    {
        const QSignalBlocker blocker(m_turnList);
        m_turnList->clear();
        for (int visibleIndex : visibleIndexes) {
            if (visibleIndex < 0 || visibleIndex >= records.size())
                continue;
            const auto& record = records.at(visibleIndex);
            auto* item = new QListWidgetItem(m_turnList);
            item->setSizeHint(QSize(0, 102));
            item->setToolTip(record.metaSummary.isEmpty() ? record.outputSummary : record.metaSummary);

            QFrame* card = makeCard(m_turnList, "QFrame { background:#ffffff; border:1px solid #e2e8f0; border-radius:16px; }");
            auto* layout = new QVBoxLayout(card);
            layout->setContentsMargins(12, 12, 12, 12);
            layout->setSpacing(8);

            auto* head = new QHBoxLayout();
            head->addWidget(makeLabel(record.listTitle, "color:#0f172a; font-size:14px; font-weight:700;", card), 1);
            QLabel* pill = makeLabel(record.statusLabel,
                                     "border-radius:999px; padding:4px 10px; font-size:11px; font-weight:700;",
                                     card);
            const QColor color = toneColor(record.statusTone);
            pill->setStyleSheet(QStringLiteral("QLabel { border:1px solid %1; background:%2; color:%1; border-radius:999px; padding:4px 10px; font-size:11px; font-weight:700; }")
                                    .arg(color.name(), color.lighter(188).name()));
            head->addWidget(pill, 0, Qt::AlignTop);
            layout->addLayout(head);
            layout->addWidget(makeLabel(firstNonEmpty({ record.outputSummary, record.metaSummary, record.inputSummary }),
                                        "color:#64748b; font-size:12px; line-height:1.5;",
                                        card));
            layout->addWidget(makeLabel(firstNonEmpty({ record.timeSummary, record.toolSummary, record.metaSummary }),
                                        "color:#94a3b8; font-size:11px;",
                                        card));
            m_turnList->setItemWidget(item, card);
        }
        if (!visibleIndexes.isEmpty()) {
            const int row = qBound(0, currentVisibleRow, visibleIndexes.size() - 1);
            m_turnList->setCurrentRow(row);
            updateDetailsForRow(row);
        } else {
            updateDetailsForRow(-1);
        }
    }
    refreshTurnCardStyles();
    m_syncingState = false;
}

void ExecutionRecordWindow::setStatusBadge(const QString& text, const QString& tone)
{
    const QColor color = toneColor(tone);
    m_historySummaryStatusBadge->setText(text);
    m_historySummaryStatusBadge->setStyleSheet(
        QStringLiteral("QLabel { border:1px solid %1; background:%2; color:%1; border-radius:999px; padding:6px 12px; font-size:12px; font-weight:700; }")
            .arg(color.name(), color.lighter(188).name()));
}

void ExecutionRecordWindow::setSceneState(const ExecutionHistory::Record* record, bool hasHistory)
{
    QString title;
    QString helper;
    const bool success = record && !record->hasError && !record->isActive;
    const bool waiting = record && record->isActive;
    const bool failure = record && record->hasError;

    if (success) {
        title = QStringLiteral("成功完成");
        helper = QStringLiteral("结果已经收束。先看结论和可信度，再决定是否继续下钻到证据层。");
    } else if (waiting) {
        title = QStringLiteral("工具执行中");
        helper = QStringLiteral("先确认最后停在了哪一步，再看工具过程和时间线末尾节点。");
    } else if (failure) {
        title = QStringLiteral("失败阻塞");
        helper = QStringLiteral("先确认阻塞点和根因，再去证据抽屉核对原始层。");
    } else if (hasHistory) {
        title = QStringLiteral("等待选择记录");
        helper = QStringLiteral("从左侧选择一条记录后，这里会告诉你当前更接近哪个典型状态。");
    } else {
        title = QStringLiteral("暂无执行记录");
        helper = QStringLiteral("发送消息后，这个区域会自动切换到成功、处理中或失败阻塞。");
    }

    m_sceneTitleLabel->setText(title);
    m_sceneHelperLabel->setText(helper);
    m_sceneSuccessChip->setStyleSheet(success
                                          ? "QLabel { border:1px solid #059669; background:#d1fae5; color:#059669; border-radius:12px; padding:8px 10px; font-size:12px; font-weight:700; }"
                                          : "QLabel { border:1px solid #e2e8f0; background:#fff; color:#64748b; border-radius:12px; padding:8px 10px; font-size:12px; font-weight:700; }");
    m_sceneWaitingChip->setStyleSheet(waiting
                                          ? "QLabel { border:1px solid #b45309; background:#fef3c7; color:#b45309; border-radius:12px; padding:8px 10px; font-size:12px; font-weight:700; }"
                                          : "QLabel { border:1px solid #e2e8f0; background:#fff; color:#64748b; border-radius:12px; padding:8px 10px; font-size:12px; font-weight:700; }");
    m_sceneFailureChip->setStyleSheet(failure
                                          ? "QLabel { border:1px solid #dc2626; background:#fee2e2; color:#dc2626; border-radius:12px; padding:8px 10px; font-size:12px; font-weight:700; }"
                                          : "QLabel { border:1px solid #e2e8f0; background:#fff; color:#64748b; border-radius:12px; padding:8px 10px; font-size:12px; font-weight:700; }");
}

void ExecutionRecordWindow::populateInsights(const ExecutionHistory::Record* record, bool hasHistory)
{
    if (!record) {
        m_quickResultValue->setText(hasHistory ? QStringLiteral("先选中一条记录。") : QStringLiteral("当前还没有执行记录。"));
        m_quickBlockerValue->setText(hasHistory ? QStringLiteral("未选中记录，暂时无法判断卡点。") : QStringLiteral("暂无阻塞信息。"));
        m_quickCauseValue->setText(hasHistory ? QStringLiteral("选中记录后显示根因判断。") : QStringLiteral("暂无根因判断。"));
        m_quickActionValue->setText(hasHistory ? QStringLiteral("选中记录后显示建议动作。") : QStringLiteral("发送消息后再回来查看。"));
        m_historySummaryInputValue->setText(hasHistory ? QStringLiteral("请选择一条记录。") : QStringLiteral("暂无输入摘要。"));
        m_historySummaryOutputValue->setText(hasHistory ? QStringLiteral("选中记录后显示输出摘要。") : QStringLiteral("暂无输出摘要。"));
        m_historySummaryToolValue->setText(hasHistory ? QStringLiteral("选中记录后显示工具摘要。") : QStringLiteral("暂无工具摘要。"));
        m_historySummaryErrorValue->setText(hasHistory ? QStringLiteral("选中记录后显示错误或提醒。") : QStringLiteral("暂无错误或提醒。"));
        return;
    }

    m_quickResultValue->setText(firstNonEmpty({ record->outputSummary, record->statusLabel }));
    m_quickBlockerValue->setText(inferBlocker(*record));
    m_quickCauseValue->setText(inferRootCause(*record));
    m_quickActionValue->setText(record->hasError ? inferSuggestedAction(*record) : inferNextStep(*record));
    m_historySummaryInputValue->setText(firstNonEmpty({ record->inputSummary, QStringLiteral("当前记录没有输入摘要。") }));
    m_historySummaryOutputValue->setText(firstNonEmpty({ record->outputSummary, QStringLiteral("当前记录没有输出摘要。") }));
    m_historySummaryToolValue->setText(firstNonEmpty({ record->toolSummary, QStringLiteral("当前记录没有独立工具摘要。") }));
    m_historySummaryErrorValue->setText(firstNonEmpty({ record->errorSummary, QStringLiteral("当前记录没有明确错误或提醒。") }));
}

void ExecutionRecordWindow::populateToolProcess(const QVector<ExecutionHistory::ToolActivity>& toolActivities)
{
    clearLayout(m_toolLayout);
    if (toolActivities.isEmpty()) {
        addFlowCard(m_toolLayout,
                    QStringLiteral("—"),
                    QStringLiteral("暂无工具过程"),
                    QStringLiteral("当前记录没有抽取到独立工具过程。"),
                    QStringLiteral("info"),
                    QString(),
                    true);
        return;
    }

    for (int i = 0; i < toolActivities.size(); ++i) {
        const auto& tool = toolActivities.at(i);
        addFlowCard(m_toolLayout,
                    QStringLiteral("步骤 %1").arg(i + 1),
                    tool.stageLabel.isEmpty() ? tool.name : QStringLiteral("%1 · %2").arg(tool.name, tool.stageLabel),
                    firstNonEmpty({ tool.outputSummary, tool.inputSummary, tool.errorSummary, QStringLiteral("当前工具没有补充摘要。") }),
                    tool.statusTone,
                    tool.statusLabel,
                    i == toolActivities.size() - 1);
    }
}

void ExecutionRecordWindow::populateMetrics(const ExecutionHistory::Record& record)
{
    m_metricStatusValue->setText(record.statusLabel);
    m_metricDurationValue->setText(record.durationDisplay.isEmpty()
                                       ? (record.isActive ? QStringLiteral("进行中") : QStringLiteral("—"))
                                       : record.durationDisplay);
    m_metricToolCountValue->setText(QStringLiteral("%1 次").arg(record.toolActivities.size()));
    m_metricExceptionValue->setText(record.hasError ? QStringLiteral("1 次") : QStringLiteral("0"));
    m_metricStageValue->setText(inferCurrentStage(record));
}

void ExecutionRecordWindow::populateDecision(const ExecutionHistory::Record& record)
{
    m_decisionConclusionValue->setText(firstNonEmpty({ record.outputSummary, QStringLiteral("当前记录没有额外结论摘要。") }));
    m_decisionBlockerValue->setText(inferBlocker(record));
    m_decisionNextValue->setText(inferNextStep(record));
}

void ExecutionRecordWindow::populateDiagnosis(const ExecutionHistory::Record& record)
{
    m_diagnosisRootCauseValue->setText(inferRootCause(record));
    m_diagnosisReliabilityValue->setText(inferReliability(record));
    m_diagnosisActionValue->setText(inferSuggestedAction(record));
}

void ExecutionRecordWindow::populateTimeline(const ExecutionHistory::Record& record)
{
    clearLayout(m_timelineLayout);
    int row = 0;
    auto add = [this, &row](const QString& indexText,
                            const QString& title,
                            const QString& detail,
                            const QString& tone,
                            const QString& badge = QString(),
                            bool last = false) {
        addFlowCard(m_timelineLayout, indexText, title, detail, tone, badge, last);
        ++row;
    };

    if (record.isEvent) {
        add(record.startedAtDisplay, record.kindLabel, firstNonEmpty({ record.outputSummary, record.metaSummary }), record.statusTone, record.statusLabel, record.toolActivities.isEmpty());
        for (int i = 0; i < record.toolActivities.size(); ++i) {
            const auto& tool = record.toolActivities.at(i);
            add(QStringLiteral("工具"),
                tool.stageLabel.isEmpty() ? tool.name : QStringLiteral("%1 · %2").arg(tool.name, tool.stageLabel),
                firstNonEmpty({ tool.outputSummary, tool.inputSummary, tool.errorSummary }),
                tool.statusTone,
                tool.statusLabel,
                i == record.toolActivities.size() - 1);
        }
        return;
    }

    add(record.startedAtDisplay,
        QStringLiteral("回合开始"),
        firstNonEmpty({ record.inputSummary, QStringLiteral("当前记录没有输入摘要。") }),
        QStringLiteral("info"),
        QStringLiteral("开始"));

    const QJsonArray segments = record.interactionFactsLayer.value("segments").toArray();
    if (!segments.isEmpty()) {
        const QJsonObject firstRequest = segments.first().toObject().value("request").toObject();
        add(record.startedAtDisplay,
            QStringLiteral("模型首段请求"),
            firstNonEmpty({
                firstRequest.value("model").toString().trimmed().isEmpty() ? QString() : QStringLiteral("模型 %1").arg(firstRequest.value("model").toString().trimmed()),
                firstRequest.value("messages").toArray().isEmpty() ? QString() : QStringLiteral("%1 条消息上下文").arg(firstRequest.value("messages").toArray().size()),
                firstRequest.value("tools").toArray().isEmpty() ? QString() : QStringLiteral("暴露 %1 个可用工具").arg(firstRequest.value("tools").toArray().size()),
                QStringLiteral("已发起一段模型请求。")
            }),
            QStringLiteral("info"),
            QStringLiteral("请求"));

        for (int i = 0; i < segments.size(); ++i) {
            const QJsonObject segment = segments.at(i).toObject();
            const QJsonObject response = segment.value("response").toObject();
            if (!response.isEmpty()) {
                const QString finishReason = finishReasonOf(response);
                const bool toolCall = (finishReason == QLatin1String("tool_calls"));
                add(QStringLiteral("第 %1 段").arg(i + 1),
                    toolCall ? QStringLiteral("模型返回工具调用") : QStringLiteral("模型返回结果"),
                    toolCall ? QStringLiteral("模型要求进入工具链继续处理。")
                             : (finishReason.isEmpty() ? QStringLiteral("模型返回了一段结果。")
                                                       : QStringLiteral("finish_reason=%1。").arg(finishReason)),
                    toolCall ? QStringLiteral("warning") : QStringLiteral("success"),
                    toolCall ? QStringLiteral("工具调用") : QStringLiteral("结果"));
            }
        }
    }

    for (int i = 0; i < record.toolActivities.size(); ++i) {
        const auto& tool = record.toolActivities.at(i);
        add(QStringLiteral("工具"),
            tool.stageLabel.isEmpty() ? tool.name : QStringLiteral("%1 · %2").arg(tool.name, tool.stageLabel),
            firstNonEmpty({ tool.outputSummary, tool.inputSummary, tool.errorSummary }),
            tool.statusTone,
            tool.statusLabel);
    }

    add(record.finishedAtDisplay.isEmpty() ? record.startedAtDisplay : record.finishedAtDisplay,
        record.hasError ? QStringLiteral("执行失败") : (record.isActive ? QStringLiteral("等待完成") : QStringLiteral("本轮结束")),
        record.hasError ? inferBlocker(record) : firstNonEmpty({ record.outputSummary, inferBlocker(record) }),
        record.hasError ? QStringLiteral("error") : (record.isActive ? QStringLiteral("warning") : QStringLiteral("success")),
        record.statusLabel,
        true);
}

void ExecutionRecordWindow::renderEvidenceLayer(QPlainTextEdit* view, const QString& title, const QJsonObject& layer)
{
    if (!view)
        return;

    if (layer.isEmpty()) {
        view->setPlainText(QStringLiteral("// %1\n// 当前层暂无内容。").arg(title));
        return;
    }

    view->setPlainText(QStringLiteral("// %1\n%2")
                           .arg(title, QString::fromUtf8(QJsonDocument(layer).toJson(QJsonDocument::Indented)).trimmed()));
}

void ExecutionRecordWindow::applyRecord(const ExecutionHistory::Record& record)
{
    m_historySummaryTypeValue->setText(record.kindLabel);
    m_historySummaryTimeValue->setText(record.timeSummary.isEmpty() ? QStringLiteral("当前记录未提供开始/完成时间。") : record.timeSummary);
    m_historySummaryMetaValue->setText(record.metaSummary.isEmpty() ? QStringLiteral("当前记录没有额外关键信息。") : record.metaSummary);
    setStatusBadge(record.statusLabel, record.statusTone);
    setSceneState(&record, true);
    populateMetrics(record);
    populateDecision(record);
    populateDiagnosis(record);
    populateInsights(&record, true);
    populateTimeline(record);
    populateToolProcess(record.toolActivities);
    renderEvidenceLayer(m_summaryLayerView, QStringLiteral("展示摘要层"), record.summaryLayer);
    renderEvidenceLayer(m_eventLayerView, QStringLiteral("事件层"), record.eventFactsLayer);
    renderEvidenceLayer(m_interactionLayerView, QStringLiteral("交互事实层"), record.interactionFactsLayer);
    renderEvidenceLayer(m_auditLayerView, QStringLiteral("审计层（未建设）"), record.auditLayer);
}

void ExecutionRecordWindow::resetRecord(bool hasHistory)
{
    m_historySummaryTypeValue->setText(hasHistory ? QStringLiteral("未选择记录") : QStringLiteral("未开始"));
    m_historySummaryTimeValue->setText(hasHistory ? QStringLiteral("选中记录后显示开始时间、完成时间与耗时。")
                                                  : QStringLiteral("当前还没有可以展示的时间信息。"));
    m_historySummaryMetaValue->setText(hasHistory ? QStringLiteral("选中记录后显示 request_id / turn_id / 模型等信息。")
                                                  : QStringLiteral("暂无 request_id / 模型 / 运行标识。"));
    setStatusBadge(hasHistory ? QStringLiteral("待查看") : QStringLiteral("暂无记录"),
                   hasHistory ? QStringLiteral("neutral") : QStringLiteral("warning"));
    setSceneState(nullptr, hasHistory);
    populateInsights(nullptr, hasHistory);
    m_metricStatusValue->setText(hasHistory ? QStringLiteral("待查看") : QStringLiteral("暂无记录"));
    m_metricDurationValue->setText(QStringLiteral("—"));
    m_metricToolCountValue->setText(QStringLiteral("0 次"));
    m_metricExceptionValue->setText(QStringLiteral("0"));
    m_metricStageValue->setText(hasHistory ? QStringLiteral("未选择") : QStringLiteral("未开始"));
    m_decisionConclusionValue->setText(hasHistory ? QStringLiteral("请先选择一条执行记录。") : QStringLiteral("当前还没有执行记录。"));
    m_decisionBlockerValue->setText(hasHistory ? QStringLiteral("尚未选中记录，无法判断阻塞点。") : QStringLiteral("暂无阻塞信息。"));
    m_decisionNextValue->setText(hasHistory ? QStringLiteral("选中记录后可继续查看问题定位与证据层。") : QStringLiteral("发送消息后再回来看这里。"));
    m_diagnosisRootCauseValue->setText(hasHistory ? QStringLiteral("选中记录后显示根因判断。") : QStringLiteral("暂无根因判断。"));
    m_diagnosisReliabilityValue->setText(hasHistory ? QStringLiteral("选中记录后显示可信度。") : QStringLiteral("暂无可信度。"));
    m_diagnosisActionValue->setText(hasHistory ? QStringLiteral("选中记录后显示建议动作。") : QStringLiteral("暂无建议动作。"));
    clearLayout(m_timelineLayout);
    addFlowCard(m_timelineLayout,
                QStringLiteral("—"),
                hasHistory ? QStringLiteral("未选择记录") : QStringLiteral("暂无执行记录"),
                hasHistory ? QStringLiteral("请从左侧选择一条记录开始分析。") : QStringLiteral("发送消息后，这里会出现关键执行节点。"),
                QStringLiteral("info"),
                QString(),
                true);
    populateToolProcess({});
    renderEvidenceLayer(m_summaryLayerView, QStringLiteral("展示摘要层"), QJsonObject());
    renderEvidenceLayer(m_eventLayerView, QStringLiteral("事件层"), QJsonObject());
    renderEvidenceLayer(m_interactionLayerView, QStringLiteral("交互事实层"), QJsonObject());
    renderEvidenceLayer(m_auditLayerView, QStringLiteral("审计层（未建设）"), QJsonObject());
}

void ExecutionRecordWindow::updateDetailsForRow(int row)
{
    if (row < 0 || row >= m_visibleIndexes.size()) {
        resetRecord(!m_records.isEmpty());
        return;
    }
    const int index = m_visibleIndexes.at(row);
    if (index < 0 || index >= m_records.size()) {
        resetRecord(!m_records.isEmpty());
        return;
    }
    applyRecord(m_records.at(index));
}

void ExecutionRecordWindow::refreshTurnCardStyles()
{
    const int currentRow = m_turnList->currentRow();
    for (int row = 0; row < m_turnList->count(); ++row) {
        auto* card = qobject_cast<QFrame*>(m_turnList->itemWidget(m_turnList->item(row)));
        if (!card)
            continue;
        const int recordIndex = row < m_visibleIndexes.size() ? m_visibleIndexes.at(row) : -1;
        const QColor color = (recordIndex >= 0 && recordIndex < m_records.size()) ? toneColor(m_records.at(recordIndex).statusTone) : QColor("#2563eb");
        const bool active = (row == currentRow);
        card->setStyleSheet(
            QStringLiteral("QFrame { background:%1; border:1px solid %2; border-radius:16px; }")
                .arg(active ? color.lighter(192).name() : QStringLiteral("#ffffff"),
                     active ? color.lighter(130).name() : QStringLiteral("#e2e8f0")));
    }
}

void ExecutionRecordWindow::addFlowCard(QVBoxLayout* layout,
                                        const QString& indexText,
                                        const QString& title,
                                        const QString& detail,
                                        const QString& tone,
                                        const QString& badgeText,
                                        bool isLast)
{
    if (!layout)
        return;

    QWidget* row = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(10);

    QLabel* time = makeLabel(indexText.isEmpty() ? QStringLiteral("—") : indexText,
                             "color:#64748b; font-size:12px;",
                             row);
    time->setAlignment(Qt::AlignRight | Qt::AlignTop);
    time->setFixedWidth(84);
    rowLayout->addWidget(time, 0);

    QWidget* axis = new QWidget(row);
    auto* axisLayout = new QVBoxLayout(axis);
    axisLayout->setContentsMargins(0, 0, 0, 0);
    axisLayout->setSpacing(4);
    const QColor color = toneColor(tone);
    QFrame* dot = new QFrame(axis);
    dot->setFixedSize(14, 14);
    dot->setStyleSheet(QStringLiteral("QFrame { background:%1; border-radius:7px; }").arg(color.name()));
    axisLayout->addWidget(dot, 0, Qt::AlignHCenter);
    QFrame* line = new QFrame(axis);
    line->setFixedWidth(2);
    line->setMinimumHeight(42);
    line->setVisible(!isLast);
    line->setStyleSheet(QStringLiteral("QFrame { background:%1; }").arg(color.lighter(180).name()));
    axisLayout->addWidget(line, 1, Qt::AlignHCenter);
    rowLayout->addWidget(axis, 0);

    QFrame* card = makeCard(row, "QFrame { background:#ffffff; border:1px solid #e2e8f0; border-radius:14px; }");
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 12, 12, 12);
    cardLayout->setSpacing(6);
    auto* head = new QHBoxLayout();
    QLabel* titleLabel = makeLabel(title, "font-size:14px; font-weight:700;", card);
    titleLabel->setStyleSheet(QStringLiteral("color:%1; font-size:14px; font-weight:700;").arg(color.name()));
    head->addWidget(titleLabel, 1);
    if (!badgeText.isEmpty()) {
        QLabel* badge = makeLabel(badgeText, "padding:4px 10px; font-size:11px; font-weight:700;", card);
        badge->setStyleSheet(QStringLiteral("QLabel { border:1px solid %1; background:%2; color:%1; border-radius:999px; padding:4px 10px; font-size:11px; font-weight:700; }")
                                 .arg(color.name(), color.lighter(188).name()));
        head->addWidget(badge, 0, Qt::AlignTop);
    }
    cardLayout->addLayout(head);
    cardLayout->addWidget(makeLabel(detail, "color:#64748b; font-size:12px; line-height:1.6;", card));
    rowLayout->addWidget(card, 1);
    layout->addWidget(row);
}

QString ExecutionRecordWindow::inferCurrentStage(const ExecutionHistory::Record& record) const
{
    if (record.hasError)
        return QStringLiteral("已终止");
    if (record.isActive)
        return QStringLiteral("等待后续结果");
    if (record.isEvent)
        return QStringLiteral("事件已记录");
    if (record.hasToolCalls)
        return QStringLiteral("工具链已完成");
    return QStringLiteral("已结束");
}

QString ExecutionRecordWindow::inferRootCause(const ExecutionHistory::Record& record) const
{
    if (record.isEvent)
        return QStringLiteral("这是一条运行事件，不是完整用户回合。");
    if (record.hasError)
        return QStringLiteral("本轮在执行过程中失败，需要结合工具过程和证据层确认根因。");
    if (record.isActive)
        return QStringLiteral("当前不是失败，而是尚未闭环，正在等待后续结果。");
    return record.hasToolCalls ? QStringLiteral("属于标准“模型 -> 工具 -> 结果收束”路径。")
                               : QStringLiteral("本轮未进入复杂工具链，主要是模型直接完成。");
}

QString ExecutionRecordWindow::inferReliability(const ExecutionHistory::Record& record) const
{
    if (record.hasError)
        return QStringLiteral("低：关键步骤失败，不应直接采信。");
    if (record.isActive)
        return QStringLiteral("中：仍在处理中，需等待最终结果。");
    if (record.isEvent)
        return QStringLiteral("中：事件可信，但只覆盖局部阶段信息。");
    return record.hasToolCalls ? QStringLiteral("高：已有完整结果，且工具过程已收束。")
                               : QStringLiteral("高：本轮已直接完成，没有明显异常。");
}

QString ExecutionRecordWindow::inferSuggestedAction(const ExecutionHistory::Record& record) const
{
    if (record.hasError)
        return QStringLiteral("先看证据层和工具过程，确认根因后再决定是否重试。");
    if (record.isActive)
        return QStringLiteral("继续等待，并重点关注时间线末尾节点。");
    if (record.isEvent)
        return QStringLiteral("把它当作辅助线索，再查看相邻主回合记录。");
    return QStringLiteral("可以直接采纳结果；如需复核，再下钻到证据层。");
}

QString ExecutionRecordWindow::inferBlocker(const ExecutionHistory::Record& record) const
{
    if (record.hasError)
        return firstNonEmpty({ record.errorSummary, QStringLiteral("本轮失败，但尚未提取到明确错误摘要。") });
    if (record.isActive)
        return QStringLiteral("当前回合尚未拿到最终结果，仍在等待模型或工具返回。");
    if (record.isEvent)
        return QStringLiteral("无明显阻塞；这条记录主要用于解释运行阶段。");
    return QStringLiteral("无明显阻塞，链路已经完成。");
}

QString ExecutionRecordWindow::inferNextStep(const ExecutionHistory::Record& record) const
{
    if (record.hasError)
        return QStringLiteral("修复阻塞原因后重试，必要时核对事件层和交互事实层。");
    if (record.isActive)
        return QStringLiteral("继续等待；若长时间不变，再判断是否需要中断或重试。");
    if (record.isEvent)
        return QStringLiteral("继续查看主回合记录，确认这条事件的上下文。");
    return QStringLiteral("如无疑问可直接使用结果；如需排障，再打开证据抽屉。");
}

void ExecutionRecordWindow::onTurnSelectionChanged(int row)
{
    updateDetailsForRow(row);
    refreshTurnCardStyles();
    if (!m_syncingState)
        emit visibleRowChanged(row);
}
