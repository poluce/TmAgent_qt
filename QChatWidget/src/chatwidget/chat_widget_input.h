#ifndef CHAT_WIDGET_INPUT_H
#define CHAT_WIDGET_INPUT_H

#include <QString>
#include <QStringList>
#include <QWidget>

class ChatWidgetCommandRegistry;

class QTextEdit;
class QToolButton;
class QListWidget;
class QListWidgetItem;
class QResizeEvent;
class QObject;
class QEvent;
class QFrame;
class QMenu;
class QAction;

class ChatWidgetInputBase : public QWidget {
    Q_OBJECT

public:
    explicit ChatWidgetInputBase(QWidget* parent = nullptr) : QWidget(parent) { }

signals:
    void messageSent(const QString& content);
    void stopRequested();
    void commandExecuted(const QString& command, const QStringList& arguments, const QString& rawText);
};

class ChatWidgetInput : public ChatWidgetInputBase {
    Q_OBJECT

public:
    explicit ChatWidgetInput(QWidget* parent = nullptr);
    ~ChatWidgetInput() override;

    ChatWidgetCommandRegistry* commandRegistry() const;
    void setEmojiList(const QStringList& emojis);

signals:
    void voiceStartRequested();
    void voiceStopRequested();
    void emojiSelected(const QString& emoji);
    void richTextToggled(bool enabled);
    void draftChanged(const QString& text);
    void imageSelected(const QStringList& paths);

public slots:
    void setSendingState(bool sending);
    void setDraftText(const QString& text);
    void setPlaceholderText(const QString& text);
    QString draftText() const;

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onSendClicked();
    void onInputTextChanged(const QString& text);
    void onCommandClicked(QListWidgetItem* item);
    void onVoiceClicked();
    void onPickImage();
    void onPickFile();
    void onEmojiPicked(QAction* action);
    void onRichTextToggled(bool checked);

private:
    void setupUi();
    QToolButton* createToolButton(const QString& objectName, const QString& tooltip,
                                  const QIcon& icon, const QString& fallbackText,
                                  QWidget* parent);
    void updateCommandMenu(const QString& text);
    bool tryApplyCommand(const QString& text);
    void positionCommandMenu();
    void updateInputEditHeight();
    void updateVoiceButtonState();
    void setSending(bool sending);

    QFrame* m_inputBar;
    QTextEdit* m_inputEdit;
    QToolButton* m_sendButton;
    QToolButton* m_voiceButton;
    QToolButton* m_plusButton;
    QToolButton* m_emojiButton;
    QToolButton* m_richTextButton;
    QMenu* m_plusMenu;
    QMenu* m_emojiMenu;
    QAction* m_pickImageAction;
    QAction* m_pickFileAction;
    QListWidget* m_commandMenu;
    ChatWidgetCommandRegistry* m_commandRegistry;
    bool m_isRecording = false;
    bool m_isSending = false;
    bool m_richTextEnabled = false;
};

#endif // CHAT_WIDGET_INPUT_H
