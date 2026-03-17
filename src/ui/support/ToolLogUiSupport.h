#ifndef TOOLLOGUISUPPORT_H
#define TOOLLOGUISUPPORT_H

class QWidget;
class ToolLogWidget;
struct ToolExecutionEvent;

namespace ToolLogUiSupport {

ToolLogWidget* ensureToolLogWindow(ToolLogWidget*& window, QWidget* parent = nullptr);
void showToolLogWindow(ToolLogWidget*& window, QWidget* parent = nullptr);
void logToolEvent(ToolLogWidget* window, const ToolExecutionEvent& event);

} // namespace ToolLogUiSupport

#endif // TOOLLOGUISUPPORT_H
