#ifndef TOOLDISPATCHER_H
#define TOOLDISPATCHER_H

#include <QObject>
#include <QJsonObject>
#include <QList>
#include "LLMAgent.h"

/**
 * @brief 工具调度器 - 负责分发和执行工具调用
 * 
 * 职责:
 *   - 接收工具调用请求
 *   - 路由到对应的工具实现
 *   - 返回执行结果
 *   - 提供所有可用工具的 Schema 定义
 * 
 * 设计原则:
 *   - 单一职责: 只负责工具调度，不涉及 UI 显示
 *   - 开闭原则: 新增工具只需添加分发逻辑
 */
class ToolDispatcher : public QObject {
    Q_OBJECT
public:
    explicit ToolDispatcher(QObject *parent = nullptr);
    
    /**
     * @brief 获取所有可用工具的 Schema 定义
     * @return 工具列表，用于注册到 LLMAgent
     */
    static QList<Tool> getAllToolSchemas();
    
    /**
     * @brief 分发工具调用
     * @param toolName 工具名称
     * @param input 输入参数 (JSON 格式)
     * @return 执行结果字符串
     */
    QString dispatch(const QString& toolName, const QJsonObject& input);
    
signals:
    /// 工具开始执行
    void toolStarted(const QString& toolName, const QString& description);
    
    /// 工具执行完成
    void toolCompleted(const QString& toolName, bool success, const QString& summary);

private:
    // 各工具的执行函数
    QString executeCreateFile(const QJsonObject& input);
    QString executeCommand(const QJsonObject& input);
    QString executeViewFile(const QJsonObject& input);
    QString executeReadFileLines(const QJsonObject& input);
};

#endif // TOOLDISPATCHER_H
