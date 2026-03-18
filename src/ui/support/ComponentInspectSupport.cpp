#include "ComponentInspectSupport.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QEvent>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QTextEdit>
#include <QWidget>

namespace {

QString clampValue(const QString& value, int maxChars = 160)
{
    const QString trimmed = value.simplified();
    if (trimmed.size() <= maxChars)
        return trimmed;
    return trimmed.left(maxChars) + QStringLiteral("...");
}

QString widgetPrimaryText(QWidget* widget)
{
    if (!widget)
        return QString();

    if (auto* button = qobject_cast<QAbstractButton*>(widget))
        return clampValue(button->text());
    if (auto* label = qobject_cast<QLabel*>(widget))
        return clampValue(label->text());
    if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
        const QString text = lineEdit->text().trimmed();
        if (!text.isEmpty())
            return clampValue(text);
        return clampValue(lineEdit->placeholderText());
    }
    if (auto* combo = qobject_cast<QComboBox*>(widget))
        return clampValue(combo->currentText());
    if (auto* group = qobject_cast<QGroupBox*>(widget))
        return clampValue(group->title());
    if (auto* tabBar = qobject_cast<QTabBar*>(widget))
        return clampValue(tabBar->tabText(tabBar->currentIndex()));
    if (auto* plain = qobject_cast<QPlainTextEdit*>(widget))
        return clampValue(plain->toPlainText());
    if (auto* textEdit = qobject_cast<QTextEdit*>(widget))
        return clampValue(textEdit->toPlainText());

    return QString();
}

QString describeWidget(QWidget* widget)
{
    if (!widget)
        return QStringLiteral("<null>");

    QString desc = QString::fromLatin1(widget->metaObject()->className());
    if (!widget->objectName().trimmed().isEmpty())
        desc += QStringLiteral("#{%1}").arg(widget->objectName().trimmed());

    const QString primaryText = widgetPrimaryText(widget);
    if (!primaryText.isEmpty())
        desc += QStringLiteral("[text=\"%1\"]").arg(primaryText);
    else if (!widget->windowTitle().trimmed().isEmpty())
        desc += QStringLiteral("[title=\"%1\"]").arg(clampValue(widget->windowTitle()));

    return desc;
}

QString widgetPath(QWidget* widget)
{
    QStringList parts;
    QWidget* current = widget;
    while (current) {
        parts.prepend(describeWidget(current));
        current = current->parentWidget();
    }
    return parts.join(QStringLiteral(" > "));
}

QString buildComponentInfo(QWidget* widget)
{
    if (!widget)
        return QStringLiteral("component: <null>");

    const QRect geometry = widget->geometry();
    const QPoint globalTopLeft = widget->mapToGlobal(QPoint(0, 0));
    QStringList lines;
    lines << QStringLiteral("component.class=%1").arg(QString::fromLatin1(widget->metaObject()->className()));
    lines << QStringLiteral("component.object_name=%1")
                 .arg(widget->objectName().trimmed().isEmpty() ? QStringLiteral("<empty>") : widget->objectName().trimmed());

    const QString primaryText = widgetPrimaryText(widget);
    if (!primaryText.isEmpty())
        lines << QStringLiteral("component.text=%1").arg(primaryText);
    if (!widget->windowTitle().trimmed().isEmpty())
        lines << QStringLiteral("component.window_title=%1").arg(clampValue(widget->windowTitle()));
    if (!widget->toolTip().trimmed().isEmpty())
        lines << QStringLiteral("component.tool_tip=%1").arg(clampValue(widget->toolTip()));
    if (!widget->whatsThis().trimmed().isEmpty())
        lines << QStringLiteral("component.whats_this=%1").arg(clampValue(widget->whatsThis()));

    lines << QStringLiteral("component.visible=%1").arg(widget->isVisible() ? QStringLiteral("true") : QStringLiteral("false"));
    lines << QStringLiteral("component.enabled=%1").arg(widget->isEnabled() ? QStringLiteral("true") : QStringLiteral("false"));
    lines << QStringLiteral("component.local_geometry=x:%1 y:%2 w:%3 h:%4")
                 .arg(geometry.x()).arg(geometry.y()).arg(geometry.width()).arg(geometry.height());
    lines << QStringLiteral("component.global_top_left=x:%1 y:%2")
                 .arg(globalTopLeft.x()).arg(globalTopLeft.y());
    lines << QStringLiteral("component.path=%1").arg(widgetPath(widget));
    lines << QStringLiteral("component.top_level=%1").arg(describeWidget(widget->window()));

    return lines.join(QLatin1Char('\n'));
}

class ComponentInspectorEventFilter : public QObject {
public:
    explicit ComponentInspectorEventFilter(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::ContextMenu)
            return QObject::eventFilter(watched, event);

        auto* contextEvent = static_cast<QContextMenuEvent*>(event);
        if (contextEvent->reason() != QContextMenuEvent::Mouse)
            return QObject::eventFilter(watched, event);
        if (!(contextEvent->modifiers() & Qt::AltModifier))
            return QObject::eventFilter(watched, event);

        QWidget* watchedWidget = qobject_cast<QWidget*>(watched);
        QWidget* target = QApplication::widgetAt(contextEvent->globalPos());
        if (!target)
            target = watchedWidget;
        if (!target || qobject_cast<QMenu*>(target))
            return QObject::eventFilter(watched, event);

        QMenu menu;
        QAction* copyInfoAction = menu.addAction(QStringLiteral("复制组件信息"));
        QAction* selected = menu.exec(contextEvent->globalPos());
        if (selected == copyInfoAction)
            QApplication::clipboard()->setText(buildComponentInfo(target));

        return true;
    }
};

} // namespace

namespace ComponentInspectSupport {

void install(QObject* owner)
{
    if (!qApp)
        return;

    static ComponentInspectorEventFilter* filter = nullptr;
    if (filter)
        return;

    filter = new ComponentInspectorEventFilter(owner ? owner : qApp);
    qApp->installEventFilter(filter);
}

} // namespace ComponentInspectSupport
