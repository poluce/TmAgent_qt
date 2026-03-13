#ifndef CHAT_WIDGET_VIEW_H
#define CHAT_WIDGET_VIEW_H

#include "chat_widget_delegate.h"
#include "chat_widget_model.h"
#include <QList>
#include <QPoint>
#include <QString>
#include <QWidget>

class QListView;

class ChatWidgetView : public QWidget {
    Q_OBJECT

public:
    explicit ChatWidgetView(QWidget* parent = nullptr);
    ~ChatWidgetView();

    ChatWidgetModel* model() const;
    void setModel(ChatWidgetModel* model);

    void setMessages(const QList<ChatWidgetMessage>& messages);
    void appendMessages(const QList<ChatWidgetMessage>& messages);
    void prependMessages(const QList<ChatWidgetMessage>& messages);

    void setDelegateStyle(const ChatWidgetDelegate::Style& style);
    ChatWidgetDelegate::Style delegateStyle() const;
    void scrollToBottom();
    void refreshLayout();

signals:
    void avatarClicked(const QString& sender, bool isMine, int row);
    void selfAvatarClicked(const QString& senderId, int row);
    void memberAvatarClicked(const QString& senderId, const QString& displayName, int row);
    void messageSelected(const QString& messageId);
    void messageContextMenuRequested(const QString& messageId, const QPoint& globalPos);
    void messageActionRequested(const QString& action, const QString& messageId, const QString& content);
    void imageClicked(const QString& messageId, const QString& imagePath);
    void voicePlayToggled(const QString& messageId, const QString& voicePath, bool play);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();

    QListView* m_chatView;
    ChatWidgetModel* m_model;
    ChatWidgetDelegate* m_delegate;
};

#endif // CHAT_WIDGET_VIEW_H
