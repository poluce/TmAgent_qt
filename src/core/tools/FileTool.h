#ifndef FILETOOL_H
#define FILETOOL_H

#include <QString>

class QJsonArray;
class QJsonObject;

class FileTool {
public:
    // 原有的静态常量
    static constexpr const char* CREATE_FILE = "create_file";
    static constexpr const char* VIEW_FILE = "view_file";
    static constexpr const char* READ_FILE_LINES = "read_file_lines";
    static constexpr const char* REPLACE_IN_FILE = "replace_in_file";
    static constexpr const char* DELETE_FILE = "delete_file";
    static constexpr const char* LIST_DIRECTORY = "list_directory";
    static constexpr const char* GREP_SEARCH = "grep_search";
    static constexpr const char* FIND_BY_NAME = "find_by_name";
    static constexpr const char* INSERT_CONTENT = "insert_content";
    static constexpr const char* MULTI_REPLACE_IN_FILE = "multi_replace_in_file";

    // ==================== 工具执行入口（接收 JSON 参数） ====================

    static QString executeCreateFile(const QJsonObject& input);
    static QString executeViewFile(const QJsonObject& input);
    static QString executeReadFileLines(const QJsonObject& input);
    static QString executeReplaceInFile(const QJsonObject& input);
    static QString executeDeleteFile(const QJsonObject& input);
    static QString executeListDirectory(const QJsonObject& input);
    static QString executeGrepSearch(const QJsonObject& input);
    static QString executeFindByName(const QJsonObject& input);
    static QString executeInsertContent(const QJsonObject& input);
    static QString executeMultiReplaceInFile(const QJsonObject& input);
    static QString executeSendFile(const QJsonObject& input);

    // ==================== 工具实现（核心函数） ====================

    static QString createFile(const QString& directory, const QString& filename, const QString& content);
    static QString convertMsysPath(const QString& path);
    static QString readFile(const QString& filePath);
    static QString readFileContent(const QString& filePath);
    static QString readFileLines(const QString& filePath, int startLine, int endLine);
    static QString replaceInFile(const QString& filePath, const QString& targetContent, const QString& replacementContent);
    static QString deleteFile(const QString& filePath);
    static QString multiReplaceInFile(const QString& filePath, const QJsonArray& replacements);
    static QString listDirectory(const QString& dirPath, bool recursive);
    static QString grepSearch(const QString& pattern, const QString& directory, const QString& filePattern);
    static QString findByName(const QString& pattern, const QString& directory);
    static QString insertContent(const QString& filePath, int lineNumber, const QString& content);

private:
    static QString resolveWorkspaceDir(const QJsonObject& input);
    static QString resolvePathUnderWorkspace(const QString& path, const QString& workspaceDir);
    static bool isPathInsideWorkspace(const QString& targetPath, const QString& workspaceDir);
    static QString ensurePathUnderWorkspace(const QString& path, const QString& workspaceDir, const QString& fieldName);
    static QString checkWritePermission(const QString& path);
};

#endif // FILETOOL_H
