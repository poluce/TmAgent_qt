# QChatWidget 设计令牌 (Design Tokens)

> **版本**: v1.0  
> **更新时间**: 2026-01-27  
> **维护方式**: TmAgent 主仓库内嵌运行组件

---

## 📋 概述

本文档定义了 QChatWidget 项目的所有设计令牌,包括颜色、间距、圆角、字体等视觉规范。所有 QSS 样式文件必须严格遵循这些令牌,确保 UI 风格的一致性。

---

## 🎨 颜色体系

### 主色调 (Primary)

用于主要操作按钮、选中状态、链接等强调元素。

| 令牌名称          | 颜色值    | 用途     | 示例                     |
| ----------------- | --------- | -------- | ------------------------ |
| `primary`         | `#4b7bec` | 默认主色 | 发送按钮、导入按钮       |
| `primary-hover`   | `#3b6fe0` | 鼠标悬停 | 按钮 hover 状态          |
| `primary-pressed` | `#2f62d6` | 鼠标按下 | 按钮 pressed 状态        |
| `primary-light`   | `#e9f1ff` | 浅色背景 | 选中项背景、菜单项 hover |

### 中性色 (Neutral)

用于背景、边框、文字等基础元素。

| 令牌名称   | 颜色值    | 用途           | 示例                |
| ---------- | --------- | -------------- | ------------------- |
| `white`    | `#ffffff` | 纯白背景       | 卡片、输入框、菜单  |
| `gray-50`  | `#f9fafb` | 最浅灰背景     | 页面背景            |
| `gray-100` | `#f5f5f5` | 浅灰背景       | 聊天背景、次级背景  |
| `gray-200` | `#e5e7eb` | 边框色         | 输入框边框、分割线  |
| `gray-300` | `#d1d5db` | 深边框色       | 输入框 focus 前边框 |
| `gray-400` | `#f4f6f9` | 次要按钮背景   | 取消按钮、测试按钮  |
| `gray-500` | `#eef2f7` | 次要按钮 hover | 灰色按钮 hover      |
| `gray-600` | `#4b5563` | 次级文字       | 提示文字、说明文字  |
| `gray-700` | `#374151` | 深色文字       | 按钮文字            |
| `gray-900` | `#111827` | 主文字色       | 正文、标题          |

### 渐变色 (Gradient)

| 令牌名称            | 渐变值                                                                    | 用途         |
| ------------------- | ------------------------------------------------------------------------- | ------------ |
| `gradient-input-bg` | `qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f7f7f8, stop:1 #eceff3)` | 输入区域背景 |

---

## 📐 圆角半径 (Border Radius)

| 令牌名称        | 数值   | 用途     | 示例                   |
| --------------- | ------ | -------- | ---------------------- |
| `radius-small`  | `6px`  | 小圆角   | 输入框、列表项、搜索框 |
| `radius-medium` | `10px` | 中圆角   | 菜单、图标按钮         |
| `radius-large`  | `12px` | 大圆角   | 主要按钮、输入栏容器   |
| `radius-xlarge` | `14px` | 超大圆角 | 输入栏外框             |

---

## 📏 间距 (Spacing)

### 内边距 (Padding)

| 令牌名称      | 数值   | 用途     | 示例             |
| ------------- | ------ | -------- | ---------------- |
| `padding-xs`  | `4px`  | 超小间距 | 紧凑元素         |
| `padding-sm`  | `6px`  | 小间距   | 按钮垂直内边距   |
| `padding-md`  | `8px`  | 中等间距 | 输入框、菜单项   |
| `padding-lg`  | `10px` | 大间距   | 按钮水平内边距   |
| `padding-xl`  | `12px` | 超大间距 | 宽按钮水平内边距 |
| `padding-2xl` | `14px` | 特大间距 | 主要按钮         |
| `padding-3xl` | `16px` | 极大间距 | 菜单项水平内边距 |

### 组合间距

| 用途          | 数值       | 应用组件               |
| ------------- | ---------- | ---------------------- |
| 图标按钮      | `6px 10px` | Plus 按钮、语音按钮    |
| 文字按钮 (小) | `4px 12px` | 更多按钮               |
| 文字按钮 (中) | `6px 14px` | 发送按钮、导入按钮     |
| 菜单项        | `8px 12px` | 命令菜单项             |
| 菜单项 (宽)   | `6px 16px` | 输入菜单项、列表菜单项 |
| 输入框        | `8px`      | QLineEdit              |
| 搜索框        | `6px 10px` | 搜索输入框             |

---

## 🔤 字体 (Typography)

### 字体族 (Font Family)

```css
font-family: 'Segoe UI', 'Microsoft YaHei', sans-serif;
```

### 字体大小 (Font Size)

| 令牌名称          | 数值   | 用途     |
| ----------------- | ------ | -------- |
| `font-size-base`  | `14px` | 默认文字 |
| `font-size-large` | `16px` | 按钮文字 |

### 字体粗细 (Font Weight)

| 令牌名称               | 数值  | 用途               |
| ---------------------- | ----- | ------------------ |
| `font-weight-normal`   | `400` | 普通文字           |
| `font-weight-medium`   | `500` | 按钮文字           |
| `font-weight-semibold` | `600` | 强调文字、更多按钮 |
| `font-weight-bold`     | `700` | 选中项             |

---

## 📦 组件尺寸 (Component Sizes)

### 最小尺寸

| 组件类型 | 最小宽度 | 最小高度 | 备注                 |
| -------- | -------- | -------- | -------------------- |
| 图标按钮 | `36px`   | -        | Plus、语音按钮       |
| 小按钮   | `40px`   | `34px`   | 更多按钮             |
| 文字按钮 | `64px`   | -        | 发送按钮             |
| 标准按钮 | `80px`   | -        | 导入、测试、取消按钮 |
| 列表项   | -        | `40px`   | Provider 列表项      |

---

## 🎯 命名规范

### ID 命名规则

- **格式**: 统一使用 `camelCase` (小驼峰命名)
- **前缀**: 使用组件名作为前缀,避免冲突

**示例**:
```css
✅ #chatListWidget
✅ #chatWidgetInputBar
✅ #providerList
❌ #provider_list (不使用下划线)
```

### 颜色值书写规则

- **格式**: 统一使用**小写十六进制**
- **长度**: 优先使用 6 位完整格式

**示例**:
```css
✅ #4b7bec
✅ #ffffff
❌ #4B7BEC (不使用大写)
❌ #fff (避免简写,保持一致性)
```

### QSS 属性书写顺序

按以下顺序组织 CSS 属性,提高可读性:

```css
QPushButton {
    /* 1. 布局相关 */
    min-width: 80px;
    min-height: 34px;
    padding: 6px 14px;
    margin: 2px 5px;
    
    /* 2. 外观相关 */
    background: #4b7bec;
    border: none;
    border-radius: 12px;
    
    /* 3. 文字相关 */
    color: white;
    font-size: 16px;
    font-weight: 500;
    
    /* 4. 其他 */
    outline: none;
}
```

---

## 🔄 状态样式规范

### 交互状态优先级

```
normal → hover → pressed → disabled
```

### 按钮状态标准

```css
/* 主要按钮 (Primary Button) */
QPushButton {
    background: #4b7bec;  /* normal */
}
QPushButton:hover {
    background: #3b6fe0;  /* hover */
}
QPushButton:pressed {
    background: #2f62d6;  /* pressed */
}

/* 次要按钮 (Secondary Button) */
QPushButton {
    background: #f4f6f9;  /* normal */
}
QPushButton:hover {
    background: #eef2f7;  /* hover */
}
QPushButton:pressed {
    background: #e2e8f0;  /* pressed */
}
```

### 列表项状态标准

```css
QListWidget::item {
    /* normal */
}
QListWidget::item:hover:!selected {
    background: #f3f4f6;  /* hover 但未选中 */
}
QListWidget::item:selected {
    background: #e9f1ff;  /* selected */
}
```

---

## 📚 使用指南

### 如何在 QSS 中使用设计令牌

**❌ 错误示例** (硬编码颜色):
```css
QPushButton {
    background: #4b7bec;
    border-radius: 12px;
}
```

**✅ 正确示例** (引用全局样式):
```css
/* 在 global.qss 中定义 */
.primary-button {
    background: #4b7bec;
    border-radius: 12px;
    /* ... */
}

/* 在组件 QSS 中引用 */
#sendButton {
    /* 继承 .primary-button 样式 */
}
```

### 新增组件样式检查清单

在添加新组件样式时,请确认:

- [ ] 颜色值是否来自设计令牌表?
- [ ] 圆角大小是否符合规范?
- [ ] 间距是否使用标准值?
- [ ] ID 命名是否使用 camelCase?
- [ ] 颜色值是否使用小写十六进制?
- [ ] 属性书写顺序是否正确?

---

## 🔧 维护说明

### 更新流程

1. 提出设计令牌变更需求
2. 在本文档中更新令牌定义
3. 更新 `global.qss` 中的对应样式
4. 测试所有受影响的组件
5. 更新版本号和更新时间

### 版本历史

| 版本 | 日期       | 变更说明                                 |
| ---- | ---------- | ---------------------------------------- |
| v1.0 | 2026-01-27 | 初始版本,统一现有三个 QSS 文件的设计令牌 |

---

## 📖 参考资料

- [Qt Style Sheets Reference](https://doc.qt.io/qt-5/stylesheet-reference.html)
- [Material Design Color System](https://material.io/design/color)
- [Tailwind CSS Design Tokens](https://tailwindcss.com/docs/customizing-colors)
