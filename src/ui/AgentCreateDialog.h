#ifndef AGENTCREATEDIALOG_H
#define AGENTCREATEDIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QPlainTextEdit;

/**
 * @brief Agent 创建对话框
 *
 * 提供名称输入和系统提示词输入，用于创建新的 Agent Identity。
 */
class AgentCreateDialog : public QDialog {
    Q_OBJECT
public:
    explicit AgentCreateDialog(QWidget* parent = nullptr);

    QString agentName() const;
    QString systemPrompt() const;

private:
    QLineEdit* m_nameEdit = nullptr;
    QPlainTextEdit* m_promptEdit = nullptr;
};

#endif // AGENTCREATEDIALOG_H
