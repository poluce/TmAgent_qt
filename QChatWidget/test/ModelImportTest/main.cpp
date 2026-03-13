#include <QApplication>
#include <QDebug>
#include <QTimer>
#include "modelconfig/model_config_import_page.h"

static ModelConfigProvider makeProvider(
    const QString &id, const QString &name, const QString &desc,
    const QString &apiKeyPlaceholder,
    const QString &modelPlaceholder, const QString &modelDefault,
    const QString &urlPlaceholder, const QString &urlDefault)
{
    ModelConfigProvider p{id, name, desc};
    if (!apiKeyPlaceholder.isEmpty()) {
        p.fields << ModelConfigField{"apiKey", "API 密钥", apiKeyPlaceholder, "", true, true};
    }
    p.fields << ModelConfigField{"modelId", "模型名称", modelPlaceholder, modelDefault};
    p.fields << ModelConfigField{"baseUrl", "接口地址", urlPlaceholder, urlDefault};
    return p;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    ModelConfigImportPage w;
    w.setWindowTitle("Model Import Test (Library Mode)");
    w.resize(800, 500);

    QList<ModelConfigProvider> defaults;
    defaults << makeProvider("deepseek", "DeepSeek", "中国高性能 AI 模型",
                             "sk-...", "deepseek-chat", "deepseek-chat",
                             "https://api.deepseek.com", "https://api.deepseek.com")
             << makeProvider("openai", "OpenAI", "全球领先的 AI 语言模型",
                             "sk-...", "gpt-4o", "gpt-4o",
                             "https://api.openai.com/v1", "https://api.openai.com/v1")
             << makeProvider("claude", "Claude", "Anthropic 强大的 AI 模型",
                             "sk-ant-...", "claude-3-5-sonnet", "claude-3-5-sonnet",
                             "https://api.anthropic.com/v1", "https://api.anthropic.com/v1")
             << makeProvider("ollama", "Ollama", "本地运行的各类型开源模型",
                             QString(), "llama3", "llama3",
                             "http://localhost:11434", "http://localhost:11434");
    w.setProviders(defaults);

    w.addProvider(makeProvider("gemini", "Gemini", "Google 强大的 AI 服务",
                               "在此输入密钥", "gemini-1.5-pro", "gemini-1.5-pro",
                               "https://generativelanguage.googleapis.com", ""));

    QObject::connect(&w, &ModelConfigImportPage::importRequested, [](const QVariantMap &config) {
        qDebug() << "Import Requested with dynamic config:" << config;
    });

    QObject::connect(&w, &ModelConfigImportPage::cancelled, []() {
        qDebug() << "Import Cancelled";
    });

    QObject::connect(&w, &ModelConfigImportPage::testConnectionRequested,
                     [&w](const QVariantMap &config) {
        qDebug() << "Test Connection Requested:" << config;
        // UI-only demo: 模拟异步验证结果回调
        QTimer::singleShot(800, [&w]() {
            w.setTestStatus(ModelConfigImportPage::TestStatus::Success,
                            QStringLiteral("模拟成功"));
        });
    });

    QObject::connect(&w, &ModelConfigImportPage::importFromFileRequested, []() {
        qDebug() << "Import From File Requested";
    });

    QObject::connect(&w, &ModelConfigImportPage::exportRequested,
                     [](const QVariantMap &config) {
        qDebug() << "Export Requested:" << config;
    });

    QObject::connect(&w, &ModelConfigImportPage::resetRequested,
                     [](const QString &providerId) {
        qDebug() << "Reset Requested for provider:" << providerId;
    });

    QObject::connect(&w, &ModelConfigImportPage::dirtyChanged, [](bool dirty) {
        qDebug() << "Dirty Changed:" << dirty;
    });

    w.show();

    return a.exec();
}
