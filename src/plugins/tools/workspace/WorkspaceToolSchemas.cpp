#include "WorkspaceToolSchemas.h"

#include "core/tools/ToolSchemaSupport.h"

QList<Tool> workspaceTools()
{
    QList<Tool> tools;
    tools.append(makeToolSchema(
        QStringLiteral("create_file"),
        QStringLiteral("在指定目录创建一个文本文件"),
        QJsonObject {
            { QStringLiteral("directory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("目标目录路径，例如: E:/test")) },
            { QStringLiteral("filename"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件名，例如: hello.txt")) },
            { QStringLiteral("content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件内容；未指定则创建空文件")) }
        },
        QStringList { QStringLiteral("directory"), QStringLiteral("filename") }));
    tools.append(makeToolSchema(
        QStringLiteral("view_file"),
        QStringLiteral("读取文件的完整内容。返回文件路径、大小、行数和完整内容。"),
        QJsonObject { { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要读取的文件绝对路径")) } },
        QStringList { QStringLiteral("file_path") }));
    tools.append(makeToolSchema(
        QStringLiteral("read_file_lines"),
        QStringLiteral("读取文件的指定行范围。"),
        QJsonObject {
            { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要读取的文件绝对路径")) },
            { QStringLiteral("start_line"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("起始行号（从 1 开始）")) },
            { QStringLiteral("end_line"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("结束行号（包含该行）")) }
        },
        QStringList { QStringLiteral("file_path"), QStringLiteral("start_line"), QStringLiteral("end_line") }));
    tools.append(makeToolSchema(
        QStringLiteral("replace_in_file"),
        QStringLiteral("替换文件中的指定内容。"),
        QJsonObject {
            { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要修改的文件绝对路径")) },
            { QStringLiteral("target_content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要被替换的原始内容（精确匹配）")) },
            { QStringLiteral("replacement_content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("替换后的新内容")) }
        },
        QStringList { QStringLiteral("file_path"), QStringLiteral("target_content"), QStringLiteral("replacement_content") }));
    tools.append(makeToolSchema(
        QStringLiteral("delete_file"),
        QStringLiteral("删除指定文件。"),
        QJsonObject { { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要删除的文件绝对路径")) } },
        QStringList { QStringLiteral("file_path") }));
    tools.append(makeToolSchema(
        QStringLiteral("list_directory"),
        QStringLiteral("列出目录中的文件和子目录。"),
        QJsonObject {
            { QStringLiteral("directory_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要列出的目录绝对路径")) },
            { QStringLiteral("recursive"), makePropertySchema(QStringLiteral("boolean"), QStringLiteral("是否递归列出子目录（默认 false）")) }
        },
        QStringList { QStringLiteral("directory_path") }));
    tools.append(makeToolSchema(
        QStringLiteral("grep_search"),
        QStringLiteral("在文件内容中搜索包含指定文本的行。"),
        QJsonObject {
            { QStringLiteral("pattern"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要搜索的文本或正则表达式")) },
            { QStringLiteral("directory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("搜索目录路径")) },
            { QStringLiteral("file_pattern"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件名模式（如 *.cpp）")) }
        },
        QStringList { QStringLiteral("pattern"), QStringLiteral("directory") }));
    tools.append(makeToolSchema(
        QStringLiteral("find_by_name"),
        QStringLiteral("按文件名模式搜索文件。"),
        QJsonObject {
            { QStringLiteral("pattern"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件名模式（如 *.cpp）")) },
            { QStringLiteral("directory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("搜索目录路径")) }
        },
        QStringList { QStringLiteral("pattern"), QStringLiteral("directory") }));
    tools.append(makeToolSchema(
        QStringLiteral("insert_content"),
        QStringLiteral("在文件指定行之后插入内容。"),
        QJsonObject {
            { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要修改的文件绝对路径")) },
            { QStringLiteral("line_number"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("在此行之后插入；0 表示文件开头")) },
            { QStringLiteral("content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要插入的内容")) }
        },
        QStringList { QStringLiteral("file_path"), QStringLiteral("line_number"), QStringLiteral("content") }));

    QJsonObject replacementItems;
    replacementItems.insert(QStringLiteral("type"), QStringLiteral("object"));
    replacementItems.insert(
        QStringLiteral("properties"),
        QJsonObject {
            { QStringLiteral("target_content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要被替换的原始内容")) },
            { QStringLiteral("replacement_content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("替换后的新内容")) }
        });
    replacementItems.insert(
        QStringLiteral("required"),
        requiredFieldsArray(QStringList { QStringLiteral("target_content"), QStringLiteral("replacement_content") }));

    tools.append(makeToolSchema(
        QStringLiteral("multi_replace_in_file"),
        QStringLiteral("一次替换文件中多处不连续的内容。"),
        QJsonObject {
            { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要修改的文件绝对路径")) },
            { QStringLiteral("replacements"),
              makePropertySchema(
                  QStringLiteral("array"),
                  QStringLiteral("替换操作数组，每个元素包含 target_content 和 replacement_content"),
                  QJsonObject { { QStringLiteral("items"), replacementItems } }) }
        },
        QStringList { QStringLiteral("file_path"), QStringLiteral("replacements") }));
    tools.append(makeToolSchema(
        QStringLiteral("send_file"),
        QStringLiteral("将内容保存为文件发送给用户。"),
        QJsonObject {
            { QStringLiteral("file_name"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件名（含扩展名）")) },
            { QStringLiteral("content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件的完整内容")) },
            { QStringLiteral("description"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件的简短描述（可选）")) }
        },
        QStringList { QStringLiteral("file_name"), QStringLiteral("content") }));
    tools.append(makeToolSchema(
        QStringLiteral("apply_patch"),
        QStringLiteral("应用结构化补丁（Patch）。"),
        QJsonObject {
            { QStringLiteral("patchText"), makePropertySchema(QStringLiteral("string"), QStringLiteral("完整的补丁文本，必须以 '*** Begin Patch' 开始")) }
        },
        QStringList { QStringLiteral("patchText") }));
    return tools;
}
