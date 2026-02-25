#ifndef BUILDSYSTEMADAPTER_H
#define BUILDSYSTEMADAPTER_H

#include <QObject>
#include <QString>

/**
 * @brief 构建系统适配器
 *
 * 核心任务：根据不同的构建系统（qmake/cmake）生成 compile_commands.json
 * 让 Agent 接入陌生项目时能自动准备好 LSP 环境。
 */
class BuildSystemAdapter : public QObject {
    Q_OBJECT
public:
    explicit BuildSystemAdapter(QObject* parent = nullptr);

    /**
     * @brief 为指定项目根目录生成编译数据库
     * @param rootPath 项目根目录（包含 .pro 或 CMakeLists.txt）
     * @return 是否成功生成
     */
    bool prepareCompileCommands(const QString& rootPath);

private:
    bool handleQmake(const QString& rootPath);
    bool handleCmake(const QString& rootPath);

    // 调用外部工具如 compiledb 或使用 qmake 导出
    bool runCompiledb(const QString& rootPath);
};

#endif // BUILDSYSTEMADAPTER_H
