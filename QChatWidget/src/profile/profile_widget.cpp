#include "profile_widget.h"
#include "qss_utils.h"

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

// ==========================================
// DetailItemWidget 实现
// ==========================================
DetailItemWidget::DetailItemWidget(const QString& key, QWidget* parent)
    : QWidget(parent), m_key(key)
{
}

void DetailItemWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        emit itemClicked(m_key);
    }
    QWidget::mouseReleaseEvent(event);
}

// ==========================================
// ProfileWidget 实现
// ==========================================

namespace {
constexpr int kAvatarSize = 64;
constexpr int kDetailTitleWidth = 70;
constexpr int kHeaderGapHeight = 10;
constexpr int kBottomButtonHeight = 45;

QPixmap buildRoundedAvatar(const QPixmap& source, int size)
{
    QPixmap scaled = source.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap rounded(size, size);
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addEllipse(0, 0, size, size);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, scaled);
    return rounded;
}
} // namespace

ProfileWidget::ProfileWidget(QWidget* parent) : QWidget(parent)
{
    setupUi();

    connect(m_msgButton, &QPushButton::clicked, this, &ProfileWidget::messageClicked);
    connect(m_moreButton, &QToolButton::clicked, this, &ProfileWidget::moreOptionsClicked);
}

void ProfileWidget::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    mainLayout->addWidget(createContentWidget(), 1);
    mainLayout->addWidget(createBottomBar());

    updateHeaderLabels();
}

QWidget* ProfileWidget::createContentWidget()
{
    QWidget* contentWidget = new QWidget(this);
    contentWidget->setObjectName("ContentWidget");
    contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 20);
    contentLayout->setSpacing(0);

    QFrame* headerGap = new QFrame();
    headerGap->setFixedHeight(kHeaderGapHeight);
    headerGap->setObjectName("GapFrame");

    contentLayout->addWidget(createHeaderWidget());
    contentLayout->addWidget(headerGap);
    contentLayout->addWidget(createGroupTitle());
    contentLayout->addWidget(createDetailGroupWidget());
    contentLayout->addStretch();
    return contentWidget;
}

QWidget* ProfileWidget::createHeaderWidget()
{
    QWidget* headerWidget = new QWidget();
    headerWidget->setObjectName("HeaderWidget");
    headerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(20, 30, 20, 30);
    headerLayout->setSpacing(15);
    headerLayout->setAlignment(Qt::AlignVCenter);

    m_avatarLabel = new QLabel();
    m_avatarLabel->setFixedSize(kAvatarSize, kAvatarSize);
    m_avatarLabel->setScaledContents(true);
    m_avatarLabel->setObjectName("AvatarLabel");
    m_defaultAvatar = QPixmap(kAvatarSize, kAvatarSize);
    m_defaultAvatar.fill(QColor(220, 220, 220));
    m_avatarLabel->setPixmap(buildRoundedAvatar(m_defaultAvatar, kAvatarSize));

    QVBoxLayout* infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(5);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setAlignment(Qt::AlignVCenter);

    m_nameLabel = new QLabel(m_defaultUserName);
    m_nameLabel->setObjectName("NameLabel");
    m_nameLabel->setWordWrap(false);
    m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_nameLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

    m_idLabel = new QLabel(m_tmIdPrefix + m_defaultTmId);
    m_idLabel->setObjectName("IdLabel");
    m_idLabel->setWordWrap(false);
    m_idLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_idLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

    m_moreButton = new QToolButton();
    m_moreButton->setText("•••");
    m_moreButton->setCursor(Qt::PointingHandCursor);
    m_moreButton->setObjectName("MoreButton");
    m_moreButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    infoLayout->addWidget(m_nameLabel);
    infoLayout->addWidget(m_idLabel);

    headerLayout->addWidget(m_avatarLabel, 0, Qt::AlignVCenter);
    headerLayout->addLayout(infoLayout, 1);
    headerLayout->addWidget(m_moreButton, 0, Qt::AlignTop);

    return headerWidget;
}

QLabel* ProfileWidget::createGroupTitle() const
{
    QLabel* groupTitle = new QLabel(tr("详细信息"));
    groupTitle->setObjectName("DetailGroupTitle");
    groupTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return groupTitle;
}

QWidget* ProfileWidget::createDetailGroupWidget()
{
    QFrame* detailGroup = new QFrame();
    detailGroup->setObjectName("DetailGroup");
    detailGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_detailListLayout = new QVBoxLayout(detailGroup);
    m_detailListLayout->setContentsMargins(16, 8, 16, 8);
    m_detailListLayout->setSpacing(0);
    return detailGroup;
}

QWidget* ProfileWidget::createBottomBar()
{
    QWidget* bottomBar = new QWidget();
    bottomBar->setObjectName("BottomBar");
    QHBoxLayout* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(20, 15, 20, 15);

    m_msgButton = new QPushButton();
    m_msgButton->setText(tr(" 发消息"));
    m_msgButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_msgButton->setCursor(Qt::PointingHandCursor);
    m_msgButton->setObjectName("MsgButton");
    m_msgButton->setFixedHeight(kBottomButtonHeight);

    bottomLayout->addWidget(m_msgButton);
    return bottomBar;
}

void ProfileWidget::addDetailItem(const QString& title, const QString& content, bool isEditable, int maxLines)
{
    if (!m_detailListLayout)
        return;
    QWidget* item = createDetailWidget(title, content, isEditable, maxLines);
    m_detailListLayout->addWidget(item);
}

void ProfileWidget::addSeparator()
{
    if (!m_detailListLayout)
        return;
    m_detailListLayout->addWidget(createSeparatorFrame());
}

void ProfileWidget::clearDetails()
{
    if (!m_detailListLayout)
        return;

    QLayoutItem* item;
    while ((item = m_detailListLayout->takeAt(0)) != nullptr) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

QWidget* ProfileWidget::createDetailWidget(const QString& title, const QString& content, bool isEditable, int maxLines)
{
    QWidget* item;
    if (isEditable) {
        DetailItemWidget* interactiveItem = new DetailItemWidget(title);
        connect(interactiveItem, &DetailItemWidget::itemClicked, this, &ProfileWidget::detailItemClicked);
        item = interactiveItem;
        item->setCursor(Qt::PointingHandCursor);
    } else {
        item = new QWidget();
    }

    QHBoxLayout* layout = new QHBoxLayout(item);
    layout->setContentsMargins(0, 10, 0, 10);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignTop);

    QLabel* titleLbl = new QLabel(title);
    titleLbl->setFixedWidth(kDetailTitleWidth);
    titleLbl->setObjectName("DetailTitle");

    QLabel* contentLbl = new QLabel(content);
    contentLbl->setObjectName("DetailContent");
    contentLbl->setWordWrap(true);
    if (maxLines > 0) {
        const int maxHeight = contentLbl->fontMetrics().lineSpacing() * maxLines + 4;
        contentLbl->setMaximumHeight(maxHeight);
        contentLbl->setToolTip(content);
    }
    if (!isEditable)
        contentLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    contentLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    layout->addWidget(titleLbl);
    layout->addWidget(contentLbl);

    if (isEditable) {
        QLabel* arrowLbl = new QLabel(QStringLiteral(">"));
        arrowLbl->setObjectName("DetailArrow");
        arrowLbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        layout->addWidget(arrowLbl);
    }

    return item;
}

QFrame* ProfileWidget::createSeparatorFrame()
{
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setObjectName("DetailSeparator");
    line->setFixedHeight(1);
    return line;
}

void ProfileWidget::setAvatar(const QPixmap& pixmap)
{
    const QPixmap& source = pixmap.isNull() ? m_defaultAvatar : pixmap;
    if (m_avatarLabel)
        m_avatarLabel->setPixmap(buildRoundedAvatar(source, kAvatarSize));
}

void ProfileWidget::setUserName(const QString& name)
{
    m_userNameRaw = name;
    updateHeaderLabels();
}

void ProfileWidget::setTmId(const QString& uuid)
{
    m_tmIdRaw = uuid;
    updateHeaderLabels();
}

void ProfileWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateHeaderLabels();
}

void ProfileWidget::updateHeaderLabels()
{
    if (!m_nameLabel || !m_idLabel)
        return;

    const QString userName = m_userNameRaw.trimmed().isEmpty() ? m_defaultUserName : m_userNameRaw;
    const QString tmId = m_tmIdRaw.trimmed().isEmpty() ? m_defaultTmId : m_tmIdRaw;
    QString nameText = userName;
    QString idText = m_tmIdPrefix + tmId;

    const int nameWidth = m_nameLabel->width();
    const int idWidth = m_idLabel->width();
    if (nameWidth > 0)
        nameText = m_nameLabel->fontMetrics().elidedText(nameText, Qt::ElideRight, nameWidth);
    if (idWidth > 0)
        idText = m_idLabel->fontMetrics().elidedText(idText, Qt::ElideRight, idWidth);

    m_nameLabel->setText(nameText);
    m_idLabel->setText(idText);
}

void ProfileWidget::applyDefaultStyle()
{
    applyStyleSheetFile(QStringLiteral("profile_widget.qss"));
}

bool ProfileWidget::applyStyleSheetFile(const QString& fileNameOrPath)
{
    if (fileNameOrPath.trimmed().isEmpty())
        return false;
    return QssUtils::applyStyleSheetFromFile(this, fileNameOrPath);
}

void ProfileWidget::setDefaultUserName(const QString& name)
{
    m_defaultUserName = name;
    updateHeaderLabels();
}

void ProfileWidget::setDefaultTmId(const QString& id)
{
    m_defaultTmId = id;
    updateHeaderLabels();
}

void ProfileWidget::setTmIdPrefix(const QString& prefix)
{
    m_tmIdPrefix = prefix;
    updateHeaderLabels();
}
