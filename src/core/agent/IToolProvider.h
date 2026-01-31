#ifndef ITOOLPROVIDER_H
#define ITOOLPROVIDER_H

#include "ToolTypes.h"
#include <QList>

/**
 * @brief 工具提供者接口
 *
 * 负责提供工具列表与执行入口，可用于本地工具或 MCP 工具。
 */
class IToolProvider {
public:
    virtual ~IToolProvider() = default;
    virtual QList<Tool> listTools() const = 0;
    virtual ToolResult execute(const ToolCall& call) = 0;
};

#endif // ITOOLPROVIDER_H
