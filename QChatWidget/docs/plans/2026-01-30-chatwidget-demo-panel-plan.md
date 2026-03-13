# ChatWidget Demo Panel Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a control panel to `test/we_chat_style/main.cpp` that manually demonstrates all major `ChatWidget` features.

**Architecture:** Keep all changes inside the demo `main.cpp`: create a left `ChatWidget` and right-side scrollable control panel with grouped buttons that call `ChatWidget` APIs and input controls via `findChild`.

**Tech Stack:** Qt Widgets (QWidget, layouts, QPushButton, QScrollArea), existing `ChatWidget`/`ThemeManager` APIs.

---

### Task 1: Build the demo window layout and status header

**Files:**
- Modify: `test/we_chat_style/main.cpp`

**Step 1: Define a manual verification checklist (pre-change)**

Manual check (current behavior):
- Run the demo and confirm the plain ChatWidget window shows and can send messages.

**Step 2: Update includes and create the new root window layout**

Add includes for widgets/layouts used by the panel:

```cpp
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QLineEdit>
#include <QVBoxLayout>
```

Replace the top-level `ChatWidget chat;` usage with a root window + layout:

```cpp
QWidget window;
window.setWindowTitle("ChatWidget 控制面板演示");
window.resize(1100, 720);

auto* rootLayout = new QHBoxLayout(&window);
rootLayout->setContentsMargins(8, 8, 8, 8);
rootLayout->setSpacing(10);

auto* chat = new ChatWidget(&window);
chat->setObjectName("demoChatWidget");
chat->applyStyleSheetFile("chat_widget.qss");
rootLayout->addWidget(chat, 3);
```

**Step 3: Add the control panel container and status label**

```cpp
auto* panelRoot = new QWidget(&window);
auto* panelLayout = new QVBoxLayout(panelRoot);
panelLayout->setContentsMargins(8, 8, 8, 8);
panelLayout->setSpacing(10);

auto* statusLabel = new QLabel("Ready", panelRoot);
statusLabel->setWordWrap(true);
statusLabel->setObjectName("demoStatusLabel");
panelLayout->addWidget(statusLabel);

auto* scrollArea = new QScrollArea(&window);
scrollArea->setWidgetResizable(true);
scrollArea->setFrameShape(QFrame::NoFrame);
scrollArea->setMinimumWidth(320);
scrollArea->setWidget(panelRoot);
rootLayout->addWidget(scrollArea, 1);
```

**Step 4: Manual verification (post-change)**

Manual check:
- Run the demo; confirm chat area appears on the left and an empty panel with a status label on the right.

**Step 5: Commit**

```bash
git add test/we_chat_style/main.cpp
git commit -m "demo: add chat widget demo layout"
```

---

### Task 2: Add helper utilities and sample data for the demo buttons

**Files:**
- Modify: `test/we_chat_style/main.cpp`

**Step 1: Define a manual verification checklist (pre-change)**

Manual check (current behavior):
- No buttons yet; only status label.

**Step 2: Add helper lambdas for sections, buttons, and status updates**

Add these helpers after panel creation:

```cpp
auto setStatus = [statusLabel](const QString& text) {
    statusLabel->setText(text);
};

auto makeSection = [panelRoot, panelLayout](const QString& title) {
    auto* box = new QGroupBox(title, panelRoot);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    panelLayout->addWidget(box);
    return layout;
};

auto addButton = [](QVBoxLayout* layout, const QString& text) {
    auto* btn = new QPushButton(text);
    layout->addWidget(btn);
    return btn;
};
```

**Step 3: Add sample data lambdas for participants and history**

```cpp
auto now = QDateTime::currentDateTime();

auto makeHistory = [now]() {
    QList<ChatWidget::HistoryMessage> history;
    ChatWidget::HistoryMessage a;
    a.senderId = "u_bob";
    a.displayName = "Bob";
    a.content = "历史消息 1";
    a.timestamp = now.addSecs(-300);
    a.messageId = "h1";
    history.append(a);

    ChatWidget::HistoryMessage b;
    b.senderId = "u_me";
    b.displayName = "Me";
    b.content = "历史消息 2";
    b.timestamp = now.addSecs(-200);
    b.messageId = "h2";
    history.append(b);

    ChatWidget::HistoryMessage c;
    c.senderId = "u_alice";
    c.displayName = "Alice";
    c.content = "历史消息 3";
    c.timestamp = now.addSecs(-100);
    c.messageId = "h3";
    history.append(c);

    return history;
};
```

**Step 4: Manual verification (post-change)**

Manual check:
- Build succeeds; no runtime changes yet beyond layout.

**Step 5: Commit**

```bash
git add test/we_chat_style/main.cpp
git commit -m "demo: add helpers and sample data"
```

---

### Task 3: Wire up all demo buttons to ChatWidget APIs and input controls

**Files:**
- Modify: `test/we_chat_style/main.cpp`

**Step 1: Define a manual verification checklist (pre-change)**

Manual check (current behavior):
- No buttons wired; no functionality via panel yet.

**Step 2: Create sections and connect buttons**

Add sections and buttons using the helpers. Example for the “基础消息” section:

```cpp
auto* msgSection = makeSection("基础消息");

auto* addMine = addButton(msgSection, "添加我的消息");
QObject::connect(addMine, &QPushButton::clicked, [chat, setStatus]() {
    ChatWidget::MessageParams params;
    params.content = "这是一条我的消息";
    params.displayName = "Me";
    params.isMine = true;
    chat->addMessage(params);
    setStatus("添加我的消息");
});

auto* addOther = addButton(msgSection, "添加他人消息");
QObject::connect(addOther, &QPushButton::clicked, [chat, setStatus]() {
    ChatWidget::MessageParams params;
    params.content = "你好，我是 Bob";
    params.senderId = "u_bob";
    params.displayName = "Bob";
    chat->addMessage(params);
    setStatus("添加他人消息（Bob）");
});

auto* removeLast = addButton(msgSection, "移除最后一条");
QObject::connect(removeLast, &QPushButton::clicked, [chat, setStatus]() {
    chat->removeLastMessage();
    setStatus("移除最后一条消息");
});

auto* clearAll = addButton(msgSection, "清空消息");
QObject::connect(clearAll, &QPushButton::clicked, [chat, setStatus]() {
    chat->clearMessages();
    setStatus("清空所有消息");
});

auto* countBtn = addButton(msgSection, "消息计数");
QObject::connect(countBtn, &QPushButton::clicked, [chat, setStatus]() {
    setStatus(QString("当前消息数: %1").arg(chat->messageCount()));
});
```

Repeat similarly for: 流式/发送、参与者、历史、空态、输入/交互。

Key input/交互按钮示例：

```cpp
auto* inputSection = makeSection("输入与交互");

auto* showCommand = addButton(inputSection, "触发命令菜单");
QObject::connect(showCommand, &QPushButton::clicked, [chat, setStatus]() {
    auto* edit = chat->findChild<QLineEdit*>("chatWidgetInputEdit");
    if (!edit) { setStatus("未找到输入框"); return; }
    edit->setText("/t");
    edit->setFocus();
    setStatus("已设置 /t 触发命令菜单");
});

auto* toggleVoice = addButton(inputSection, "模拟语音按钮");
QObject::connect(toggleVoice, &QPushButton::clicked, [chat, setStatus]() {
    auto* voice = chat->findChild<QPushButton*>("chatWidgetInputVoiceButton");
    if (!voice) { setStatus("未找到语音按钮"); return; }
    voice->click();
    setStatus("已触发语音按钮 click");
});

auto* openPlus = addButton(inputSection, "打开附件菜单");
QObject::connect(openPlus, &QPushButton::clicked, [chat, setStatus]() {
    auto* plus = chat->findChild<QToolButton*>("chatWidgetInputPlusButton");
    if (!plus) { setStatus("未找到 + 按钮"); return; }
    plus->showMenu();
    setStatus("已打开附件菜单");
});
```

Connect avatar click signals to status updates:

```cpp
QObject::connect(chat, &ChatWidget::avatarClicked, [setStatus](const QString& name, bool isMine, int row) {
    setStatus(QString("头像点击: %1 (isMine=%2, row=%3)").arg(name).arg(isMine).arg(row));
});
QObject::connect(chat, &ChatWidget::selfAvatarClicked, [setStatus](const QString& id, int row) {
    setStatus(QString("点击自己头像: %1 row=%2").arg(id).arg(row));
});
QObject::connect(chat, &ChatWidget::memberAvatarClicked, [setStatus](const QString& id, const QString& name, int row) {
    setStatus(QString("点击成员头像: %1(%2) row=%3").arg(name, id).arg(row));
});
```

**Step 3: Manual verification (post-change)**

Manual check:
- 运行 demo，逐组点击按钮，确认状态栏提示与 ChatWidget 变化一致。
- 验证：历史消息排序/去重、参与者头像/昵称更新、空状态切换、语音按钮切换、命令菜单弹出、附件菜单弹出、头像点击信号触发。

**Step 4: Commit**

```bash
git add test/we_chat_style/main.cpp
git commit -m "demo: wire chat widget feature buttons"
```

---

### Task 4: Clean up and finalize demo polish

**Files:**
- Modify: `test/we_chat_style/main.cpp`

**Step 1: Manual verification checklist (pre-change)**

Manual check:
- All features already demonstrable; observe any layout overflow or unclear status text.

**Step 2: Adjust layout/polish text**

Examples of polish:
- `panelLayout->addStretch(1);` at end to keep spacing.
- Ensure window title and status label wording are clear.

**Step 3: Manual verification (post-change)**

Manual check:
- Verify layout stays readable when window resized.

**Step 4: Commit**

```bash
git add test/we_chat_style/main.cpp
git commit -m "demo: polish control panel layout"
```
