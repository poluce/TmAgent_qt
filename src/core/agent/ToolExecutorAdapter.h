#ifndef TOOLEXECUTORADAPTER_H
#define TOOLEXECUTORADAPTER_H

#include "IToolExecutor.h"
#include <QObject>

class ToolDispatcher;

/**
 * @brief ToolExecutorAdapter - 桥接 SDK IToolExecutor 接口到 ToolDispatcher
 * 
 * 此适配器类实现 SDK 的 IToolExecutor 接口，将工具执行请求转发到主应用的 ToolDispatcher。
 * 用于后端插件在委托会话中调用工具，而无需直接依赖 ToolDispatcher。
 */
class ToolExecutorAdapter : public QObject, public TmAgent::IToolExecutor {
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param dispatcher ToolDispatcher 实例指针
     * @param parent 父对象（用于 Qt 内存管理）
     */
    explicit ToolExecutorAdapter(ToolDispatcher* dispatcher, QObject* parent = nullptr);
    ~ToolExecutorAdapter() override;
    
    // IToolExecutor 接口实现
    TmAgent::ToolResult executeToolSync(const TmAgent::ToolCall& call) override;
    void executeToolAsync(const TmAgent::ToolCall& call,
                         std::function<void(const TmAgent::ToolResult&)> callback) override;

private:
    ToolDispatcher* m_dispatcher;
};

#endif // TOOLEXECUTORADAPTER_H
