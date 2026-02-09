#include "AgentCreateDialog.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

AgentCreateDialog::AgentCreateDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("创建 Agent"));
    resize(400, 300);

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(tr("Agent 名称:"), this));
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("例如：代码助手"));
    layout->addWidget(m_nameEdit);

    layout->addWidget(new QLabel(tr("系统提示词（可选）:"), this));
    m_promptEdit = new QPlainTextEdit(this);
    m_promptEdit->setPlaceholderText(tr("定义 Agent 的角色和行为..."));
    layout->addWidget(m_promptEdit, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (!m_nameEdit->text().trimmed().isEmpty())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString AgentCreateDialog::agentName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

QString AgentCreateDialog::systemPrompt() const
{
    return m_promptEdit ? m_promptEdit->toPlainText().trimmed() : QString();
}
