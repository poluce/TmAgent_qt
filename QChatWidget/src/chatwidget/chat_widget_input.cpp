#include "chat_widget_input.h"
#include "chat_widget_command.h"
#include <QFileDialog>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QResizeEvent>
#include <QSize>
#include <QStyle>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

namespace {
QIcon iconOrFallback(QWidget* widget, const QString& themeName, QStyle::StandardPixmap fallback)
{
    QIcon icon = QIcon::fromTheme(themeName);
    if (icon.isNull() && widget && widget->style()) {
        icon = widget->style()->standardIcon(fallback);
    }
    return icon;
}

void refreshWidgetStyle(QWidget* widget)
{
    if (!widget || !widget->style()) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}
} // namespace

ChatWidgetInput::ChatWidgetInput(QWidget* parent)
    : ChatWidgetInputBase(parent)
    , m_commandRegistry(new ChatWidgetCommandRegistry())
{
    setupUi();
}

ChatWidgetCommandRegistry* ChatWidgetInput::commandRegistry() const
{
    return m_commandRegistry;
}

ChatWidgetInput::~ChatWidgetInput()
{
    delete m_commandRegistry;
}

void ChatWidgetInput::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateInputEditHeight();
    if (m_commandMenu->isVisible()) {
        positionCommandMenu();
    }
}

QToolButton* ChatWidgetInput::createToolButton(const QString& objectName, const QString& tooltip,
                                                const QIcon& icon, const QString& fallbackText,
                                                QWidget* parent)
{
    auto* btn = new QToolButton(parent);
    btn->setObjectName(objectName);
    btn->setProperty("role", "icon");
    btn->setAutoRaise(true);
    btn->setToolTip(tooltip);
    btn->setIcon(icon);
    btn->setText(icon.isNull() ? fallbackText : QString());
    return btn;
}

void ChatWidgetInput::setupUi()
{
    setObjectName("chatWidgetInputRoot");

    m_inputBar = new QFrame(this);
    m_inputBar->setObjectName("chatWidgetInputBar");

    m_plusButton = createToolButton("chatWidgetInputPlusButton", tr("更多"),
                                     iconOrFallback(this, QStringLiteral("list-add"), QStyle::SP_FileDialogNewFolder),
                                     "+", m_inputBar);
    m_plusButton->setPopupMode(QToolButton::InstantPopup);

    m_emojiButton = createToolButton("chatWidgetInputEmojiButton", tr("表情"),
                                      QIcon::fromTheme(QStringLiteral("face-smile")),
                                      "☺", m_inputBar);
    m_emojiButton->setPopupMode(QToolButton::InstantPopup);

    m_voiceButton = createToolButton("chatWidgetInputVoiceButton", tr("语音输入"),
                                      iconOrFallback(this, QStringLiteral("audio-input-microphone"), QStyle::SP_MediaVolume),
                                      "◎", m_inputBar);

    m_inputEdit = new QTextEdit(m_inputBar);
    m_inputEdit->setObjectName("chatWidgetInputEdit");
    m_inputEdit->setPlaceholderText(tr("输入消息..."));
    m_inputEdit->setAcceptRichText(false);
    m_inputEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inputEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inputEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_inputEdit->setTabChangesFocus(true);
    m_inputEdit->installEventFilter(this);

    m_richTextButton = createToolButton("chatWidgetInputRichButton", tr("富文本"),
                                         QIcon::fromTheme(QStringLiteral("format-text-richtext")),
                                         "A", m_inputBar);
    m_richTextButton->setCheckable(true);

    m_sendButton = createToolButton("chatWidgetInputSendButton", tr("发送"),
                                     iconOrFallback(this, QStringLiteral("mail-send"), QStyle::SP_ArrowForward),
                                     "➤", m_inputBar);

    const QSize toolIconSize(20, 20);
    for (auto* btn : { m_plusButton, m_emojiButton, m_richTextButton, m_voiceButton, m_sendButton })
        btn->setIconSize(toolIconSize);

    m_plusMenu = new QMenu(this);
    m_plusMenu->setObjectName("chatWidgetInputMenu");
    m_pickImageAction = m_plusMenu->addAction(tr("图片"));
    m_pickFileAction = m_plusMenu->addAction(tr("文件"));
    m_plusButton->setMenu(m_plusMenu);

    m_emojiMenu = new QMenu(this);
    m_emojiMenu->setObjectName("chatWidgetInputEmojiMenu");
    setEmojiList({ "😀", "😂", "😍", "👍", "🎉", "🔥", "🙏", "✅", "✨", "😅" });
    m_emojiButton->setMenu(m_emojiMenu);

    m_commandMenu = new QListWidget(this);
    m_commandMenu->setObjectName("chatWidgetCommandMenu");
    m_commandMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    m_commandMenu->setFocusPolicy(Qt::NoFocus);
    m_commandMenu->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_commandMenu->setSelectionMode(QAbstractItemView::SingleSelection);
    m_commandMenu->setSpacing(2);

    QGraphicsDropShadowEffect* barShadow = new QGraphicsDropShadowEffect(this);
    barShadow->setBlurRadius(18);
    barShadow->setOffset(0, 3);
    barShadow->setColor(QColor(0, 0, 0, 45));
    m_inputBar->setGraphicsEffect(barShadow);

    QGraphicsDropShadowEffect* menuShadow = new QGraphicsDropShadowEffect(this);
    menuShadow->setBlurRadius(18);
    menuShadow->setOffset(0, 4);
    menuShadow->setColor(QColor(0, 0, 0, 45));
    m_commandMenu->setGraphicsEffect(menuShadow);

    QVBoxLayout* inputLayout = new QVBoxLayout(m_inputBar);
    inputLayout->setContentsMargins(10, 6, 10, 6);
    inputLayout->setSpacing(6);
    inputLayout->addWidget(m_inputEdit);

    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    actionLayout->addWidget(m_plusButton);
    actionLayout->addWidget(m_emojiButton);
    actionLayout->addStretch();
    actionLayout->addWidget(m_richTextButton);
    actionLayout->addWidget(m_voiceButton);
    actionLayout->addWidget(m_sendButton);
    inputLayout->addLayout(actionLayout);

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(m_inputBar);

    connect(m_sendButton, &QToolButton::clicked, this, &ChatWidgetInput::onSendClicked);
    connect(m_inputEdit, &QTextEdit::textChanged, this, [this]() {
        const QString text = m_inputEdit->toPlainText();
        onInputTextChanged(text);
        updateInputEditHeight();
    });
    connect(m_commandMenu, &QListWidget::itemClicked, this, &ChatWidgetInput::onCommandClicked);
    connect(m_voiceButton, &QToolButton::clicked, this, &ChatWidgetInput::onVoiceClicked);
    connect(m_pickImageAction, &QAction::triggered, this, &ChatWidgetInput::onPickImage);
    connect(m_pickFileAction, &QAction::triggered, this, &ChatWidgetInput::onPickFile);
    connect(m_emojiMenu, &QMenu::triggered, this, &ChatWidgetInput::onEmojiPicked);
    connect(m_richTextButton, &QToolButton::toggled, this, &ChatWidgetInput::onRichTextToggled);

    updateInputEditHeight();
    updateVoiceButtonState();
    setSending(false);
}

bool ChatWidgetInput::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_inputEdit && event && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const bool isEnter = (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter);
        if (isEnter) {
            const Qt::KeyboardModifiers mods = keyEvent->modifiers();
            if (mods.testFlag(Qt::ControlModifier)) {
                m_inputEdit->insertPlainText("\n");
                return true;
            }
            if (mods == Qt::NoModifier) {
                onSendClicked();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatWidgetInput::onSendClicked()
{
    if (m_isSending) {
        emit stopRequested();
        return;
    }

    const QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty())
        return;

    if (tryApplyCommand(text)) {
        return;
    }

    emit messageSent(text);
    m_inputEdit->clear();
    // 发送态由上层（ChatService/UI）统一驱动，避免本地乐观自锁导致短暂无法连续发送。
    // 这里不再立即切换为 sending=true。
}

void ChatWidgetInput::onInputTextChanged(const QString& text)
{
    updateCommandMenu(text);
    emit draftChanged(text);
}

void ChatWidgetInput::onCommandClicked(QListWidgetItem* item)
{
    if (!item)
        return;
    const QString cmd = item->data(Qt::UserRole).toString();
    if (!cmd.isEmpty()) {
        tryApplyCommand(cmd);
    }
}

void ChatWidgetInput::updateCommandMenu(const QString& text)
{
    if (!text.startsWith("/")) {
        m_commandMenu->hide();
        return;
    }

    const QString prefix = text.mid(1).trimmed();
    const QList<ChatWidgetCommand> matched = m_commandRegistry->matchCommands(prefix);

    m_commandMenu->clear();
    QString lastCategory;
    for (const ChatWidgetCommand& cmd : matched) {
        if (!cmd.category.isEmpty() && cmd.category != lastCategory) {
            lastCategory = cmd.category;
            QListWidgetItem* header = new QListWidgetItem(cmd.category);
            header->setFlags(Qt::NoItemFlags);
            QFont headerFont = header->font();
            headerFont.setBold(true);
            header->setFont(headerFont);
            m_commandMenu->addItem(header);
        }
        const QString label = cmd.name + "  " + cmd.description;
        QListWidgetItem* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, cmd.name);
        m_commandMenu->addItem(item);
    }

    if (m_commandMenu->count() == 0) {
        m_commandMenu->hide();
        return;
    }

    // 选中第一个可选项（跳过分类标题）
    for (int i = 0; i < m_commandMenu->count(); ++i) {
        if (m_commandMenu->item(i)->flags() & Qt::ItemIsSelectable) {
            m_commandMenu->setCurrentRow(i);
            break;
        }
    }
    positionCommandMenu();
    m_commandMenu->show();
}

bool ChatWidgetInput::tryApplyCommand(const QString& text)
{
    ChatWidgetCommandRegistry::ParsedCommand parsed = m_commandRegistry->parse(text);
    if (!parsed.valid)
        return false;

    // 所有命令统一通过信号委托给宿主
    m_inputEdit->clear();
    m_commandMenu->hide();
    emit commandExecuted(parsed.commandName, parsed.arguments, parsed.rawText);
    return true;
}

void ChatWidgetInput::positionCommandMenu()
{
    const int rowHeight = m_commandMenu->sizeHintForRow(0);
    const int maxVisibleRows = qMin(m_commandMenu->count(), 6);
    const int height = (rowHeight > 0 ? rowHeight : 24) * maxVisibleRows + 2;
    m_commandMenu->setFixedSize(m_inputBar->width(), height);

    const QPoint globalPos = m_inputBar->mapToGlobal(QPoint(0, 0));
    m_commandMenu->move(globalPos.x(), globalPos.y() - height - 6);
}

void ChatWidgetInput::setSending(bool sending)
{
    m_isSending = sending;
    m_sendButton->setProperty("sending", m_isSending);
    m_sendButton->setToolTip(m_isSending ? tr("停止生成") : tr("发送"));
    m_sendButton->setIcon(m_isSending
                              ? iconOrFallback(this, QStringLiteral("process-stop"), QStyle::SP_MediaStop)
                              : iconOrFallback(this, QStringLiteral("mail-send"), QStyle::SP_ArrowForward));
    m_sendButton->setText(m_sendButton->icon().isNull() ? (m_isSending ? "■" : "➤") : QString());
    refreshWidgetStyle(m_sendButton);
}

void ChatWidgetInput::setSendingState(bool sending)
{
    setSending(sending);
}

void ChatWidgetInput::setDraftText(const QString& text)
{
    m_inputEdit->setPlainText(text);
}

void ChatWidgetInput::setPlaceholderText(const QString& text)
{
    m_inputEdit->setPlaceholderText(text);
}

QString ChatWidgetInput::draftText() const
{
    return m_inputEdit->toPlainText();
}

void ChatWidgetInput::onVoiceClicked()
{
    m_isRecording = !m_isRecording;
    if (m_isRecording) {
        m_inputEdit->setPlaceholderText(tr("录音中...（占位）"));
        m_inputEdit->clear();
        m_commandMenu->hide();
        emit voiceStartRequested();
    } else {
        m_inputEdit->setPlaceholderText(tr("输入消息..."));
        emit voiceStopRequested();
    }
    updateVoiceButtonState();
}

void ChatWidgetInput::onPickImage()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("选择图片"), QString(),
                                                            "Images (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (!paths.isEmpty())
        emit imageSelected(paths);
}

void ChatWidgetInput::onPickFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("选择文件"), QString(),
                                                      "All Files (*.*)");
    if (!path.isEmpty())
        emit messageSent(tr("【文件】") + path);
}

void ChatWidgetInput::onEmojiPicked(QAction* action)
{
    if (!action) {
        return;
    }
    const QString emoji = action->data().toString();
    if (emoji.isEmpty()) {
        return;
    }
    m_inputEdit->insertPlainText(emoji);
    emit emojiSelected(emoji);
}

void ChatWidgetInput::onRichTextToggled(bool checked)
{
    m_richTextEnabled = checked;
    emit richTextToggled(m_richTextEnabled);
}

void ChatWidgetInput::updateInputEditHeight()
{
    if (!m_inputEdit) {
        return;
    }

    constexpr int kMinLines = 1;
    constexpr int kMaxLines = 6;

    const QFontMetrics fm(m_inputEdit->font());
    const int lineHeight = fm.lineSpacing();
    const int contentMargin = static_cast<int>(qRound(m_inputEdit->document()->documentMargin())) * 2;
    const int frame = m_inputEdit->frameWidth() * 2;
    const int minHeight = lineHeight * kMinLines + contentMargin + frame;
    const int maxHeight = lineHeight * kMaxLines + contentMargin + frame;

    const int docHeight = qCeil(m_inputEdit->document()->size().height()) + contentMargin + frame;
    const int targetHeight = qBound(minHeight, docHeight, maxHeight);
    m_inputEdit->setFixedHeight(targetHeight);
    m_inputEdit->setVerticalScrollBarPolicy(docHeight > maxHeight ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);

    // 输入区高度始终由内容驱动，避免在父布局空态时被拉伸占满。
    if (m_inputBar) {
        const int barHeight = m_inputBar->sizeHint().height();
        if (barHeight > 0) {
            setMinimumHeight(barHeight);
            setMaximumHeight(barHeight);
            updateGeometry();
        }
    }
}

void ChatWidgetInput::updateVoiceButtonState()
{
    m_voiceButton->setProperty("recording", m_isRecording);
    m_voiceButton->setToolTip(m_isRecording ? tr("停止录音") : tr("语音输入"));
    m_voiceButton->setIcon(m_isRecording
                               ? iconOrFallback(this, QStringLiteral("media-playback-stop"), QStyle::SP_MediaStop)
                               : iconOrFallback(this, QStringLiteral("audio-input-microphone"), QStyle::SP_MediaVolume));
    m_voiceButton->setText(m_voiceButton->icon().isNull() ? (m_isRecording ? "■" : "◎") : QString());
    refreshWidgetStyle(m_voiceButton);
}

void ChatWidgetInput::setEmojiList(const QStringList& emojis)
{
    m_emojiMenu->clear();
    for (const QString& emoji : emojis) {
        QAction* action = m_emojiMenu->addAction(emoji);
        action->setData(emoji);
    }
}
