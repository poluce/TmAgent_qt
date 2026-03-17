#include "ThinkingIndicatorWidget.h"
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVariantAnimation>

ThinkingIndicatorWidget::ThinkingIndicatorWidget(QWidget* parent)
    : QWidget(parent), m_isShowing(false), m_breathAlpha(230)
{
    setupUi();
    setupAnimations();

    // 悬浮层，必须设置这个避免被布局管理器限制，并能在上层绘制
    setAttribute(Qt::WA_TransparentForMouseEvents);

    // 如果父组件已经设定好大小，我们需要提前定位一次
    if (parent) {
        adjustPosition();
    }
}

ThinkingIndicatorWidget::~ThinkingIndicatorWidget()
{
}

void ThinkingIndicatorWidget::setupUi()
{
    // 固定高度和宽度约束
    setFixedHeight(36);
    setMinimumWidth(180);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(8);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setStyleSheet("color: white; font-size: 14px;");

    m_textLabel = new QLabel(this);
    m_textLabel->setStyleSheet("color: white; font-size: 13px; font-weight: bold;");
    m_textLabel->setAlignment(Qt::AlignCenter);

    layout->addWidget(m_iconLabel);
    layout->addWidget(m_textLabel);
    layout->addStretch();

    // 添加阴影效果以突显悬浮感
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(15);
    shadow->setColor(QColor(0, 0, 0, 60));
    shadow->setOffset(0, 4);
    setGraphicsEffect(shadow);

    // . 点点点 动画定时器
    m_dotTimer = new QTimer(this);
    connect(m_dotTimer, &QTimer::timeout, this, [this]() {
        if (m_currentDot == "...") {
            m_currentDot = ".";
        } else {
            m_currentDot += ".";
        }

        QString baseText = m_textLabel->property("baseText").toString();
        m_textLabel->setText(baseText + m_currentDot);

        // 自适应宽度
        adjustSize();
        adjustPosition();
    });

    hide(); // 默认隐藏
}

void ThinkingIndicatorWidget::setupAnimations()
{
    // 1. 位置动画（下滑出现/上滑消失）
    m_posAnimation = new QPropertyAnimation(this, "pos", this);
    m_posAnimation->setEasingCurve(QEasingCurve::OutBack); // 带一点点回弹的灵动效果
    m_posAnimation->setDuration(400);

    // 2. 呼吸灯动画（控制透明度）
    m_breathAnimation = new QVariantAnimation(this);
    m_breathAnimation->setStartValue(180);
    m_breathAnimation->setEndValue(245);
    m_breathAnimation->setDuration(1200);
    m_breathAnimation->setEasingCurve(QEasingCurve::InOutSine);
    m_breathAnimation->setLoopCount(-1); // 无限循环

    connect(m_breathAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_breathAlpha = value.toInt();
        update(); // 触发重绘
    });
}

void ThinkingIndicatorWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 绘制深色毛玻璃/圆角渐变背景
    QPainterPath path;
    path.addRoundedRect(rect(), height() / 2, height() / 2); // 变成胶囊形状

    // 呼吸色的主控：使用背景色 QColor(黑色，带动态 Alpha)
    QColor bgColor(30, 30, 35, m_breathAlpha);
    painter.fillPath(path, bgColor);
}

void ThinkingIndicatorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    adjustPosition();
}

void ThinkingIndicatorWidget::adjustPosition()
{
    if (!parentWidget())
        return;

    // 保持水平居中
    int parentWidth = parentWidget()->width();
    int x = (parentWidth - width()) / 2;

    // Y坐标由当前动画来管，如果不参与动画的话，我们就设置在顶部一段距离
    if (m_posAnimation->state() != QAbstractAnimation::Running) {
        if (m_isShowing) {
            move(x, 20); // 留出一点边距
        } else {
            move(x, -height() - 20);
        }
    } else {
        // 在做动画时只更新 X 以保持 Resize 时不歪
        move(x, y());
    }
}

void ThinkingIndicatorWidget::showThinking(const QString& text)
{
    if (m_isShowing) {
        updateText(text);
        return;
    }

    // 设置基础文本
    m_textLabel->setProperty("baseText", text);
    m_currentDot = ".";
    m_textLabel->setText(text + m_currentDot);
    m_iconLabel->setText("💡");

    adjustSize();
    adjustPosition();

    m_isShowing = true;
    show();
    raise(); // 拿到所有图层的最上面

    // 准备下滑动画
    if (parentWidget()) {
        int x = (parentWidget()->width() - width()) / 2;
        m_posAnimation->setStartValue(QPoint(x, -height() - 20));
        m_posAnimation->setEndValue(QPoint(x, 20)); // 下落到留白20的位置
        m_posAnimation->start();
    }

    // 启动呼吸灯和打字机倒序器
    m_breathAnimation->start();
    m_dotTimer->start(600);
}

void ThinkingIndicatorWidget::updateText(const QString& text)
{
    m_textLabel->setProperty("baseText", text);
    if (text.contains("工具")) {
        m_iconLabel->setText("🔧");
    } else if (text.contains("查")) {
        m_iconLabel->setText("🔍");
    } else {
        m_iconLabel->setText("💡");
    }
    m_textLabel->setText(text + m_currentDot);
    adjustSize();
    adjustPosition();
}

void ThinkingIndicatorWidget::hideIndicator()
{
    if (!m_isShowing)
        return;

    m_isShowing = false;

    m_breathAnimation->stop();
    m_dotTimer->stop();

    // 准备上滑收回动画
    if (parentWidget()) {
        int x = (parentWidget()->width() - width()) / 2;
        m_posAnimation->setStartValue(pos());
        m_posAnimation->setEndValue(QPoint(x, -height() - 20));
        connect(m_posAnimation, &QPropertyAnimation::finished, this, [this]() {
            if (!m_isShowing) {
                hide();
            } }, Qt::UniqueConnection); // 保证只执行一次

        m_posAnimation->start();
    } else {
        hide();
    }
}
