#include "ToolLogUiSupport.h"

#include "ToolLogWidget.h"
#include "core/agent/ToolTypes.h"

namespace ToolLogUiSupport {

ToolLogWidget* ensureToolLogWindow(ToolLogWidget*& window, QWidget* parent)
{
    if (!window)
        window = new ToolLogWidget(parent);
    return window;
}

void showToolLogWindow(ToolLogWidget*& window, QWidget* parent)
{
    ToolLogWidget* widget = ensureToolLogWindow(window, parent);
    if (!widget)
        return;
    widget->show();
    widget->raise();
    widget->activateWindow();
}

void logToolEvent(ToolLogWidget* window, const ToolExecutionEvent& event)
{
    if (window)
        window->logEvent(event);
}

} // namespace ToolLogUiSupport
