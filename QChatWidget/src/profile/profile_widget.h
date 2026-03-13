#ifndef PROFILE_WIDGET_H
#define PROFILE_WIDGET_H

#include <QPixmap>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class QFrame;
class QResizeEvent;
class QMouseEvent;

// ==========================================
// 内部辅助类：可点击的列表项
// ==========================================
class DetailItemWidget : public QWidget {
    Q_OBJECT
public:
    explicit DetailItemWidget(const QString& key, QWidget* parent = nullptr);

signals:
    void itemClicked(const QString& key);

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QString m_key;
};

// ==========================================
// 主组件类
// ==========================================
class ProfileWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProfileWidget(QWidget* parent = nullptr);

    // --- 基础信息接口 ---
    void setAvatar(const QPixmap& pixmap);
    void setUserName(const QString& name);
    void setTmId(const QString& uuid);
    void setDefaultUserName(const QString& name);
    void setDefaultTmId(const QString& id);
    void setTmIdPrefix(const QString& prefix);
    void applyDefaultStyle();
    bool applyStyleSheetFile(const QString& fileNameOrPath);

    // --- 详情列表接口 ---
    // isEditable: true 表示显示为可点击样式
    // maxLines: 0 表示不限制行数，>0 表示最多显示 N 行并截断
    void addDetailItem(const QString& title, const QString& content, bool isEditable = false, int maxLines = 0);
    void addSeparator();
    void clearDetails();

signals:
    void messageClicked();                        // 点击"发消息"
    void moreOptionsClicked();                    // 点击右上角"..."
    void detailItemClicked(const QString& title); // 点击列表项

private:
    void setupUi();
    void updateHeaderLabels();
    QWidget* createContentWidget();
    QWidget* createHeaderWidget();
    QLabel* createGroupTitle() const;
    QWidget* createDetailGroupWidget();
    QWidget* createBottomBar();

    // 内部辅助函数
    QFrame* createSeparatorFrame();
    QWidget* createDetailWidget(const QString& title, const QString& content, bool isEditable, int maxLines);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    // UI 控件
    QLabel* m_avatarLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_idLabel = nullptr;
    QPushButton* m_msgButton = nullptr;
    QToolButton* m_moreButton = nullptr;

    QVBoxLayout* m_detailListLayout = nullptr;

    QPixmap m_defaultAvatar;
    QString m_defaultUserName = QStringLiteral("agentname");
    QString m_defaultTmId = QStringLiteral("--");
    QString m_tmIdPrefix = QStringLiteral("TmId: ");
    QString m_userNameRaw;
    QString m_tmIdRaw;
};

#endif // PROFILE_WIDGET_H
