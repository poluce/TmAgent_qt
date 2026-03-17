#ifndef THINKING_INDICATOR_WIDGET_H
#define THINKING_INDICATOR_WIDGET_H

#include <QString>
#include <QWidget>

class QLabel;
class QPropertyAnimation;
class QVariantAnimation;
class QGraphicsDropShadowEffect;
class QSequentialAnimationGroup;
class QParallelAnimationGroup;
class QTimer;

class ThinkingIndicatorWidget : public QWidget {
    Q_OBJECT

public:
    explicit ThinkingIndicatorWidget(QWidget* parent = nullptr);
    ~ThinkingIndicatorWidget() override;

    // 显示思考状态
    void showThinking(const QString& text = QStringLiteral("💡 正在深度思考中..."));
    // 隐藏状态指示
    void hideIndicator();
    // 更新文本信息（如切换到工具调用状态）
    void updateText(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();
    void setupAnimations();
    void adjustPosition();

    QLabel* m_iconLabel;
    QLabel* m_textLabel;

    QPropertyAnimation* m_posAnimation;
    QVariantAnimation* m_breathAnimation;

    // 内部状态
    bool m_isShowing = false;
    int m_breathAlpha = 255;
    QString m_currentDot;

    QTimer* m_dotTimer;
};

#endif // THINKING_INDICATOR_WIDGET_H
