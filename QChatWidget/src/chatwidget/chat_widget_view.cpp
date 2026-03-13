#include "chat_widget_view.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

ChatWidgetView::ChatWidgetView(QWidget* parent) : QWidget(parent)
{
    setObjectName("chatWidgetView");
    setupUi();
}

ChatWidgetView::~ChatWidgetView() { }

ChatWidgetModel* ChatWidgetView::model() const
{
    return m_model;
}

void ChatWidgetView::setModel(ChatWidgetModel* model)
{
    if (!model || model == m_model) {
        return;
    }

    if (m_model && m_model->parent() == this) {
        m_model->deleteLater();
    }

    m_model = model;
    if (!m_model->parent()) {
        m_model->setParent(this);
    }
    m_chatView->setModel(m_model);
}

void ChatWidgetView::setupUi()
{
    m_model = new ChatWidgetModel(this);
    m_delegate = new ChatWidgetDelegate(this);

    m_chatView = new QListView(this);
    m_chatView->setObjectName("chatWidgetViewList");
    m_chatView->setModel(m_model);
    m_chatView->setItemDelegate(m_delegate);
    m_chatView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_chatView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_chatView->setFocusPolicy(Qt::NoFocus);
    m_chatView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_chatView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chatView->setResizeMode(QListView::Adjust);
    m_chatView->viewport()->installEventFilter(this);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_chatView);
}

void ChatWidgetView::setMessages(const QList<ChatWidgetMessage>& messages)
{
    m_model->setMessages(messages);
    scrollToBottom();
}

void ChatWidgetView::appendMessages(const QList<ChatWidgetMessage>& messages)
{
    m_model->appendMessages(messages);
    scrollToBottom();
}

void ChatWidgetView::prependMessages(const QList<ChatWidgetMessage>& messages)
{
    m_model->prependMessages(messages);
    refreshLayout();
}

void ChatWidgetView::setDelegateStyle(const ChatWidgetDelegate::Style& style)
{
    m_delegate->setStyle(style);
    m_chatView->viewport()->update();
}

ChatWidgetDelegate::Style ChatWidgetView::delegateStyle() const
{
    return m_delegate->style();
}

void ChatWidgetView::scrollToBottom()
{
    m_chatView->scrollToBottom();
}

void ChatWidgetView::refreshLayout()
{
    m_chatView->doItemsLayout();
    m_chatView->viewport()->update();
}

bool ChatWidgetView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_chatView->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QModelIndex index = m_chatView->indexAt(mouseEvent->pos());
        if (!index.isValid()) {
            return QWidget::eventFilter(watched, event);
        }

        if (mouseEvent->button() == Qt::LeftButton) {
            const QString messageId = index.data(ChatWidgetModel::ChatWidgetMessageIdRole).toString();
            if (!messageId.isEmpty()) {
                emit messageSelected(messageId);
            }
            m_chatView->setCurrentIndex(index);

            QStyleOptionViewItem option;
            option.initFrom(m_chatView);
            option.rect = m_chatView->visualRect(index);
            const QRect avatarHitRect = m_delegate->avatarRect(option, index);
            if (avatarHitRect.contains(mouseEvent->pos())) {
                const QString sender = index.data(ChatWidgetModel::ChatWidgetSenderRole).toString();
                const QString senderId = index.data(ChatWidgetModel::ChatWidgetSenderIdRole).toString();
                const bool isMine = index.data(ChatWidgetModel::ChatWidgetIsMineRole).toBool();
                emit avatarClicked(sender, isMine, index.row());
                if (!senderId.isEmpty()) {
                    if (isMine) {
                        emit selfAvatarClicked(senderId, index.row());
                    } else {
                        emit memberAvatarClicked(senderId, sender, index.row());
                    }
                }
            }

            // 检测文件卡片点击 → 用系统默认应用打开文件（精确命中卡片区域）
            const QRect fileHitRect = m_delegate->fileCardRect(option, index);
            if (!fileHitRect.isNull() && fileHitRect.contains(mouseEvent->pos())) {
                const int msgType = index.data(ChatWidgetModel::ChatWidgetMessageTypeRole).toInt();
                const QString filePath = index.data(ChatWidgetModel::ChatWidgetFilePathRole).toString();
                const QString imagePath = index.data(ChatWidgetModel::ChatWidgetImagePathRole).toString();
                const QString voicePath = index.data(ChatWidgetModel::ChatWidgetVoicePathRole).toString();

                if (!imagePath.isEmpty() || msgType == static_cast<int>(ChatWidgetMessage::MessageType::Image)) {
                    // 图片点击
                    emit imageClicked(messageId, imagePath);
                } else if (!voicePath.isEmpty() || msgType == static_cast<int>(ChatWidgetMessage::MessageType::Voice)) {
                    // 语音点击：切换播放状态
                    const int playState = index.data(ChatWidgetModel::ChatWidgetVoicePlayStateRole).toInt();
                    const bool isPlaying = (playState == static_cast<int>(ChatWidgetMessage::VoicePlayState::Playing));
                    emit voicePlayToggled(messageId, voicePath, !isPlaying);
                } else if (!filePath.isEmpty()) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
                }
            }
        } else if (mouseEvent->button() == Qt::RightButton) {
            const QString messageId = index.data(ChatWidgetModel::ChatWidgetMessageIdRole).toString();
            const QString content = index.data(ChatWidgetModel::ChatWidgetContentRole).toString();
            m_chatView->setCurrentIndex(index);
            emit messageContextMenuRequested(messageId, mouseEvent->globalPos());

            QMenu menu(this);
            QAction* copyAction = menu.addAction(tr("复制"));
            QAction* rememberAction = menu.addAction(tr("记住这条"));
            rememberAction->setEnabled(!content.trimmed().isEmpty());

            QAction* picked = menu.exec(mouseEvent->globalPos());
            if (!picked) {
                return QWidget::eventFilter(watched, event);
            }

            if (picked == copyAction) {
                if (!content.isEmpty()) {
                    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
                        clipboard->setText(content, QClipboard::Clipboard);
                        if (clipboard->supportsSelection())
                            clipboard->setText(content, QClipboard::Selection);
                    }
                }
                emit messageActionRequested(QStringLiteral("copy"), messageId, content);
            } else if (picked == rememberAction) {
                emit messageActionRequested(QStringLiteral("remember"), messageId, content);
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void ChatWidgetView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (event->oldSize().width() == event->size().width())
        return;

    // 宽度变化后，强制触发 delegate 重新计算 sizeHint，避免气泡间距残留旧布局。
    refreshLayout();
    QTimer::singleShot(0, this, [this]() {
        refreshLayout();
    });
}
