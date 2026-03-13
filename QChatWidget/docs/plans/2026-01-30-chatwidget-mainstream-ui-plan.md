# ChatWidget Mainstream UI Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend `ChatWidget` to provide mainstream chat UI capabilities (timestamp/status rendering, attachments, reactions, reply/forward, search highlight, selection/context menu, input enhancements) while keeping logic UI-only with clean APIs and signals.

**Architecture:** Enrich `ChatWidgetMessage`/`HistoryMessage` data model, add roles and update APIs in model/view/widget, and update the delegate to render the new metadata. Input widget exposes more UI controls and emits signals without backend dependencies.

**Tech Stack:** Qt Widgets (QWidget/QListView/QStyledItemDelegate/QMenu), existing ChatWidget modules.

---

### Task 1: Extend message data structures and model roles

**Files:**
- Modify: `src/chatwidget/chat_widget_model.h`
- Modify: `src/chatwidget/chat_widget_model.cpp`
- Modify: `src/chatwidget/chat_widget.h`

**Step 1: Update message structs**

Add enums and fields to `ChatWidgetMessage`:
- `MessageType` (Text/Image/File/System/DateSeparator)
- `MessageStatus` (Sending/Sent/Failed/Read)
- attachment fields (imagePath/filePath/fileName/fileSize)
- reply/forward fields (replyToMessageId/replySender/replyPreview/isForwarded/forwardedFrom)
- reactions (list of emoji + count)
- mentions (list of strings)

Add matching fields to `ChatWidget::HistoryMessage` so history can carry the metadata.

**Step 2: Expand model roles**

Add roles for new fields (type, status, attachments, reactions, reply, forward, mentions, system flag, search keyword).

**Step 3: Add model update helpers**

Add methods to update message by `messageId`:
- `updateMessageStatus`
- `updateMessageContent`
- `updateMessageReactions`
- `updateMessageAttachments`
- `updateMessageReply`

Also add a `setSearchKeyword` for highlight.

**Step 4: Manual verification**

Build only. No runtime changes expected yet.

**Step 5: Commit**

```bash
git add src/chatwidget/chat_widget_model.h src/chatwidget/chat_widget_model.cpp src/chatwidget/chat_widget.h
git commit -m "feat: extend chat message data model"
```

---

### Task 2: Update view APIs and signals

**Files:**
- Modify: `src/chatwidget/chat_widget_view.h`
- Modify: `src/chatwidget/chat_widget_view.cpp`
- Modify: `src/chatwidget/chat_widget.h`
- Modify: `src/chatwidget/chat_widget.cpp`

**Step 1: View update APIs**

Expose methods to update message status/reactions/attachments/search keyword via view → model.

**Step 2: Context menu and selection signals**

Add signals for:
- `messageSelected(messageId)`
- `messageContextMenuRequested(messageId, globalPos)`
- `messageActionRequested(action, messageId)` (or specific signals for copy/reply/forward/retry/react)

Add right-click menu on messages; emit signals on action triggers.

**Step 3: Manual verification**

Build only.

**Step 4: Commit**

```bash
git add src/chatwidget/chat_widget_view.h src/chatwidget/chat_widget_view.cpp src/chatwidget/chat_widget.h src/chatwidget/chat_widget.cpp
git commit -m "feat: add message actions and view update APIs"
```

---

### Task 3: Render mainstream UI in delegate

**Files:**
- Modify: `src/chatwidget/chat_widget_delegate.h`
- Modify: `src/chatwidget/chat_widget_delegate.cpp`

**Step 1: Add style fields**

Add fonts/colors for timestamp, status text, system message, selection outline, reaction chips.

**Step 2: Implement rendering**

- Show timestamp (top or bottom)
- Show status for “mine” messages
- Render system/date separator messages centered
- Render image/file bubbles
- Render reply/forward preview block
- Render reactions row
- Highlight mentions and search keyword

**Step 3: Manual verification**

Build only.

**Step 4: Commit**

```bash
git add src/chatwidget/chat_widget_delegate.h src/chatwidget/chat_widget_delegate.cpp
git commit -m "feat: render mainstream chat metadata"
```

---

### Task 4: Enhance input widget UI and signals

**Files:**
- Modify: `src/chatwidget/chat_widget_input.h`
- Modify: `src/chatwidget/chat_widget_input.cpp`
- Modify: `resources/styles/chat_widget.qss`

**Step 1: Add emoji and rich-text toggles**

Add an emoji button with a simple menu; emit `emojiSelected` and insert into input. Add `richTextToggled` signal (UI only) and a shortcut-hint label.

**Step 2: Draft APIs**

Add `setDraftText()` / `draftText()` and emit `draftChanged` on input edits.

**Step 3: Manual verification**

Build only.

**Step 4: Commit**

```bash
git add src/chatwidget/chat_widget_input.h src/chatwidget/chat_widget_input.cpp resources/styles/chat_widget.qss
git commit -m "feat: enhance chat input widget"
```

---

### Task 5: Optional demo updates

**Files:**
- Modify (optional): `test/we_chat_style/main.cpp`

Add a few sample messages to showcase images, files, system messages, and status changes.

---

## Testing

Manual checks:
- Compile and run `test/we_chat_style/WeChatStyle.pro`.
- Verify timestamps, statuses, system messages, image/file rendering, reactions, search highlight, and context menu actions.

