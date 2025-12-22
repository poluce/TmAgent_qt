#include <QCoreApplication>
#include "core/agent/LLMAgent.h"
#include "core/tools/FileTool.h"
#include "core/utils/ConfigManager.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    // 创建 Agent
    LLMAgent* agent = new LLMAgent(&app);
    
    // 设置配置 (确保已经配置了 API Key)
    // ConfigManager::setApiKey("your-api-key");
    // ConfigManager::setBaseUrl("https://api.anthropic.com");
    // ConfigManager::setModel("claude-3-5-sonnet-20241022");
    
    // 定义 create_file 工具
    Tool createFileTool;
    createFileTool.name = "create_file";
    createFileTool.description = "在指定目录创建一个文本文件";
    createFileTool.inputSchema = QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"directory", QJsonObject{
                {"type", "string"},
                {"description", "目标目录路径"}
            }},
            {"filename", QJsonObject{
                {"type", "string"},
                {"description", "文件名"}
            }},
            {"content", QJsonObject{
                {"type", "string"},
                {"description", "文件内容"}
            }}
        }},
        {"required", QJsonArray{"directory", "filename", "content"}}
    };
    
    // 注册工具
    agent->registerTool(createFileTool);
    
    // 连接工具调用信号
    QObject::connect(agent, &LLMAgent::toolCallRequested,
        [agent](const QString& toolId, const QString& toolName, const QJsonObject& input) {
        
        qDebug() << "\n========== 工具调用请求 ==========";
        qDebug() << "工具 ID:" << toolId;
        qDebug() << "工具名称:" << toolName;
        qDebug() << "参数:" << QJsonDocument(input).toJson(QJsonDocument::Indented);
        
        if (toolName == "create_file") {
            QString directory = input["directory"].toString();
            QString filename = input["filename"].toString();
            QString content = input["content"].toString();
            
            qDebug() << "\n执行文件创建...";
            qDebug() << "  目录:" << directory;
            qDebug() << "  文件名:" << filename;
            qDebug() << "  内容:" << content;
            
            // 执行工具
            QString result = FileTool::createFile(directory, filename, content);
            
            qDebug() << "  结果:" << result;
            qDebug() << "==================================\n";
            
            // 返回结果给 Agent
            agent->submitToolResult(toolId, result);
        }
    });
    
    // 连接完成信号
    QObject::connect(agent, &LLMAgent::finished,
        [&app](const QString& content) {
        qDebug() << "\n========== Claude 最终回复 ==========";
        qDebug() << content;
        qDebug() << "====================================\n";
        
        app.quit();
    });
    
    // 连接错误信号
    QObject::connect(agent, &LLMAgent::errorOccurred,
        [&app](const QString& error) {
        qDebug() << "\n========== 错误 ==========";
        qDebug() << error;
        qDebug() << "==========================\n";
        
        app.quit();
    });
    
    // 发起带工具的对话
    qDebug() << "\n========== 开始测试 ==========";
    qDebug() << "请求: 请在 E:/test 目录下创建一个名为 helloworld.txt 的文件,内容是 'Hello, World from Claude!'";
    qDebug() << "==============================\n";
    
    agent->askWithTools("请在 E:/test 目录下创建一个名为 helloworld.txt 的文件,内容是 'Hello, World from Claude!'");
    
    return app.exec();
}
