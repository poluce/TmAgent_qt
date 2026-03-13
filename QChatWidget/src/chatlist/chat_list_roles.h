#ifndef CHAT_LIST_ROLES_H
#define CHAT_LIST_ROLES_H

#include <Qt>

// 定义数据模型中的自定义角色
enum ChatListRoles {
    ChatListNameRole = Qt::UserRole + 1, // 昵称
    ChatListMessageRole,                 // 最后一条消息
    ChatListTimeRole,                    // 时间
    ChatListAvatarColorRole,             // 头像颜色
    ChatListAvatarPathRole,              // 头像图片路径（可选）
    ChatListUnreadCountRole              // 未读消息数
};

#endif // CHAT_LIST_ROLES_H
