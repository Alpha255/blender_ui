# Blender Menubar 绘制流程分析

## 概述

Blender 的 menubar（顶部菜单栏：File、Edit、Render、Window、Help）绘制流程从 Python 菜单定义开始，经过 RNA 桥接层进入 C++ 层，最终通过 GPU 批量绘制完成像素渲染。整个过程涉及约 10 个核心文件。

---

## 核心文件清单

### 菜单定义（Python）
- [space_topbar.py](scripts/startup/bl_ui/space_topbar.py) — 所有顶栏菜单定义（File、Edit、Render、Window、Help 等）
- [\_bpy_types.py](scripts/modules/_bpy_types.py) — `draw_collapsible()` 方法（控制展开/折叠行为，第 1359 行）

### Topbar 空间（C++）
- [space_topbar.cc](source/blender/editors/space_topbar/space_topbar.cc) — Topbar 空间类型注册，创建 header 区域，注册子菜单（最近文件、撤销历史）

### Header 布局与绘制（C++）
- [area.cc](source/blender/editors/screen/area.cc) — `ED_region_header_layout()`（第 3852 行）和 `ED_region_header_draw()`（第 3953 行）

### 菜单按钮创建（C++）
- [interface_layout.cc](source/blender/editors/interface/interface_layout.cc) — `Layout::menu()`（第 3027 行）、`Layout::menu_contents()`（第 3037 行）、`item_menu()`（第 2927 行）、`menutype_draw()`（第 6017 行）、`button_type_set_menu_from_pulldown()`（第 2997 行）

### 按钮定义（C++）
- [interface.cc](source/blender/editors/interface/interface.cc) — `uiDefMenuBut()`（第 6425 行）、`uiDefIconTextMenuBut()`（第 6441 行）、`block_draw()`（第 2230 行）、`blocklist_draw()`（第 3837 行）

### Widget 渲染（C++）
- [interface_widgets.cc](source/blender/editors/interface/interface_widgets.cc) — `draw_button()`（第 5398 行）、`widget_menubut()`（第 4066 行）、`widget_pulldownbut()`（第 4675 行）、`widgetbase_draw()`（第 1155 行）、`widget_draw_text_icon()`（第 2738 行）、`widget_draw_text()`（第 2345 行）

### 菜单弹出创建（C++）
- [interface_handlers.cc](source/blender/editors/interface/interface_handlers.cc) — `block_open_begin()`（第 4663 行）
- [interface_region_menu_popup.cc](source/blender/editors/interface/regions/interface_region_menu_popup.cc) — `popup_menu_create()`（第 436 行）、`block_func_POPUP()`（第 226 行）

### 菜单类型注册中心（C++）
- [wm_menu_type.cc](source/blender/windowmanager/intern/wm_menu_type.cc) — `WM_menutype_find()`、`WM_menutype_add()`、`WM_menutype_poll()`

### RNA 桥接（C++）
- [rna_ui_api.cc](source/blender/makesrna/intern/rna_ui_api.cc) — `rna_uiItemM()`（第 569 行）和 `rna_uiItemM_contents()`（第 588 行）

---

## 完整绘制流程（11 步）

### 第 1 步：菜单定义（Python）

菜单定义为继承 `bpy.types.Menu` 的 Python 类，位于 [space_topbar.py](scripts/startup/bl_ui/space_topbar.py)：

- `TOPBAR_MT_editor_menus`（第 106 行）— 顶层聚合器，包含五个主菜单
- `TOPBAR_MT_file`（第 157 行）— 文件菜单
- `TOPBAR_MT_edit`（第 511 行）— 编辑菜单
- `TOPBAR_MT_render`（第 467 行）— 渲染菜单
- `TOPBAR_MT_window`（第 555 行）— 窗口菜单
- `TOPBAR_MT_help`（第 600 行）— 帮助菜单

每个类都有 `bl_label` 属性和 `draw(self, context)` 方法，通过 `self.layout` 填充菜单项。

```python
class TOPBAR_MT_file(Menu):
    bl_label = "File"

    def draw(self, context):
        layout = self.layout
        layout.operator("wm.open_mainfile", text="Open...", icon='FILE_FOLDER')
        layout.menu("TOPBAR_MT_file_open_recent")
        # ...
```

**菜单层级结构：**
- `TOPBAR_MT_editor_menus.draw()` 调用 `layout.menu("TOPBAR_MT_file")` 等创建五个顶层条目
- 每个顶层菜单通过 `layout.operator()`、`layout.menu()`（子菜单）、`layout.separator()` 定义自己的项目
- 子菜单如 `TOPBAR_MT_file_open_recent`、`TOPBAR_MT_undo_history` 在 C++ 层定义（[space_topbar.cc](source/blender/editors/space_topbar/space_topbar.cc) 第 191-281 行），因为它们需要特殊的程序化逻辑

---

### 第 2 步：Header 布局入口（Python）

[space_topbar.py:17](scripts/startup/bl_ui/space_topbar.py#L17) — `TOPBAR_HT_upper_bar.draw()` 是绘制起点。这是 `Header` 类，其 `bl_space_type = 'TOPBAR'`。

```python
class TOPBAR_HT_upper_bar(Header):
    bl_space_type = 'TOPBAR'

    def draw_left(self, context):
        TOPBAR_MT_editor_menus.draw_collapsible(context, layout)
```

---

### 第 3 步：展开与折叠决策（Python）

[\_bpy_types.py:1359](scripts/modules/_bpy_types.py#L1359) — `draw_collapsible()` 检查 `context.area.show_menus`：

- **展开模式**：`layout.row(align=True).menu_contents(cls.__name__)` — 将菜单内联绘制为独立的水平按钮（File、Edit、Render、Window、Help 各一个按钮）
- **折叠模式**：`layout.menu(cls.__name__, icon='COLLAPSEMENU')` — 显示单个汉堡菜单图标按钮

---

### 第 4 步：RNA 桥接（Python → C++）

[rna_ui_api.cc:569](source/blender/makesrna/intern/rna_ui_api.cc#L569) — `rna_uiItemM()` 将 Python 的 `layout.menu("TOPBAR_MT_file")` 调用桥接到 C++ 的 `Layout::menu(menuname, ...)`。

[rna_ui_api.cc:588](source/blender/makesrna/intern/rna_ui_api.cc#L588) — `rna_uiItemM_contents()` 将 Python 的 `layout.menu_contents("...")` 调用桥接到 C++ 的 `Layout::menu_contents(menuname)`。

---

### 第 5 步：按钮创建（C++）

[interface_layout.cc:3027](source/blender/editors/interface/interface_layout.cc#L3027) — `Layout::menu(const StringRef menuname, ...)`：

1. 通过 `WM_menutype_find()` 查找 `MenuType*`
2. 调用 `Layout::menu(MenuType *mt, ...)`（第 3003 行）
3. 调用 `item_menu()`（第 2927 行）
4. `item_menu()` 调用 `uiDefMenuBut()` 或 `uiDefIconTextMenuBut()`（在 [interface.cc](source/blender/editors/interface/interface.cc) 第 6425 行和 6441 行定义）
5. 按钮的 `menu_create_func` 被设为 `item_menutype_func`（第 3020 行）
6. 对 Header 区域，`button_type_set_menu_from_pulldown()` 将按钮类型从 `Pulldown` 转换为 `Menu`（第 2997 行，实现在 [interface.cc:6150](source/blender/editors/interface/interface.cc#L6150)）

---

### 第 6 步：Header 布局阶段

[area.cc:3852](source/blender/editors/screen/area.cc#L3852) — `ED_region_header_layout()`：

- 遍历 `region->runtime->type->headertypes`（注册的 Header 类型列表）
- 对每个 Header：调用 `ht.poll(C, &ht)` 检查是否活跃
- 创建 `ui::Block` 和 `ui::Layout`
- 调用 `ht.draw(C, &header)` — 这会触发 Python 的 `TOPBAR_HT_upper_bar.draw()` 方法
- 调用 `block_end(C, block)` 完成 block

---

### 第 7 步：Header 绘制阶段

[area.cc:3953](source/blender/editors/screen/area.cc#L3953) — `ED_region_header_draw()`：

- 清除区域背景
- 调用 `region_draw_blocks_in_view2d()` → `ui::blocklist_draw()`（[interface.cc:3837](source/blender/editors/interface/interface.cc#L3837)）
- `blocklist_draw()` 对每个活跃 block 调用 `block_draw()`

---

### 第 8 步：Block 绘制

[interface.cc:2230](source/blender/editors/interface/interface.cc#L2230) — `block_draw()`：

- 设置视图/投影矩阵和裁剪区域
- 如适用，绘制 block 背景
- 遍历 block 中的所有按钮
- 对每个可见按钮调用 `draw_button()`

---

### 第 9 步：按钮 Widget 渲染

[interface_widgets.cc:5398](source/blender/editors/interface/interface_widgets.cc#L5398) — `draw_button()`：

根据按钮类型和 emboss 样式选择 widget 类型：

| 按钮类型 | Emboss 类型 | Widget 样式 |
|---------|------------|------------|
| `Menu` | `Emboss` | `MenuRadio`（带下拉箭头）或 `MenuIconRadio`（仅图标，无箭头） |
| `Menu` | `Pulldown` | `MenuItem` |
| `Pulldown` | `Emboss` | `Pulldown`（使用 `widget_pulldownbut()`） |

然后调用：
- `wt->state(...)` 和 `wt->custom(...)` 或 `wt->draw(...)` 绘制 widget 背景
- `wt->text(...)`（即 `widget_draw_text_icon()`，第 2738 行）绘制文本和图标

---

### 第 10 步：具体 Widget 绘制

[interface_widgets.cc:4066](source/blender/editors/interface/interface_widgets.cc#L4066) — `widget_menubut()`（用于展开的 menubar 项目）：

- 通过 `round_box_edges()` + `widgetbase_draw()` 绘制圆角按钮背景
- 通过 `shape_preset_trias_from_rect_menu()` 绘制三角下拉箭头装饰
- 右侧文本区域缩减 `(6 * height) / 10` 为箭头留空间

[interface_widgets.cc:1155](source/blender/editors/interface/interface_widgets.cc#L1155) — `widgetbase_draw()`：

- 使用 GPU 批量绘制（`draw_widgetbase_batch()`）
- 填充内部颜色、轮廓、浮雕效果和三角装饰

[interface_widgets.cc:2738](source/blender/editors/interface/interface_widgets.cc#L2738) — `widget_draw_text_icon()`：

- 图标绘制通过 `widget_draw_icon()`（第 1335 行）
- 文本绘制通过 `widget_draw_text()`（第 2345 行）
- 文本使用 **BLF（Blender Font）** 进行字形渲染，包含字体样式、对齐和 alpha 混合

---

### 第 11 步：点击后弹出下拉菜单

当用户点击 menubar 按钮时：

1. [interface_handlers.cc:4663](source/blender/editors/interface/interface_handlers.cc#L4663) — `block_open_begin()` 将按钮映射到处理器：
   - `ButtonType::Menu` → 调用 `popup_menu_create()` 并传入 `but->menu_create_func`
2. [interface_region_menu_popup.cc:436](source/blender/editors/interface/regions/interface_region_menu_popup.cc#L436) — `popup_menu_create()` 包装函数并调用 `popup_menu_create_impl()`（第 393 行）
3. `popup_menu_create_impl()` 创建 `PopupMenu`，调用 `popup_block_create()` 注册 block 创建回调（`block_func_POPUP`）
4. [interface_region_menu_popup.cc:226](source/blender/editors/interface/regions/interface_region_menu_popup.cc#L226) — `block_func_POPUP()` 运行时：
   - 调用 `popup_menu_create_block()`（第 179 行）创建带 `LayoutType::Menu` 的 UI Block
   - 调用 `pup->menu_func(C, pup->layout)` 即 `item_menutype_func` → `menutype_draw()`（[interface_layout.cc:6017](source/blender/editors/interface/interface_layout.cc#L6017)）
   - `menutype_draw()` 调用 `mt->draw(C, &menu)` 触发 Python 菜单的 `draw()` 方法
5. 下拉菜单作为独立的 UI block 渲染，带有 pulldown 风格背景，定位在 menubar 按钮下方

---

### 菜单类型注册中心

[wm_menu_type.cc](source/blender/windowmanager/intern/wm_menu_type.cc)：

- `WM_menutype_find()` — 通过 idname 在全局哈希表中查找菜单类型
- `WM_menutype_add()` — 注册新菜单类型
- `WM_menutype_poll()` — 检查菜单类型是否满足上下文条件

---

## 整体流程图

```
┌─────────────────────────────────────────────────────────────────┐
│ Python 层                                                        │
│                                                                   │
│  space_topbar.py                                                  │
│    ├── TOPBAR_MT_file (bl_label="File")                          │
│    ├── TOPBAR_MT_edit (bl_label="Edit")                          │
│    ├── TOPBAR_MT_render (bl_label="Render")                      │
│    ├── TOPBAR_MT_window (bl_label="Window")                      │
│    └── TOPBAR_MT_help (bl_label="Help")                          │
│         ↓                                                        │
│  TOPBAR_HT_upper_bar.draw() ← Header 入口                        │
│         ↓                                                        │
│  draw_collapsible() ← 展开/折叠决策                              │
│         ↓                                                        │
│  layout.menu() / layout.menu_contents()                          │
└──────────────────────┬──────────────────────────────────────────┘
                       │ RNA 桥接
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│ C++ 层                                                           │
│                                                                   │
│  rna_ui_api.cc: rna_uiItemM() / rna_uiItemM_contents()          │
│         ↓                                                        │
│  interface_layout.cc: Layout::menu() / Layout::menu_contents()   │
│         ↓                                                        │
│  interface_layout.cc: item_menu() → uiDefMenuBut()               │
│         ↓                                                        │
│  interface.cc: button_type_set_menu_from_pulldown()              │
│         ↓                                                        │
│  area.cc: ED_region_header_layout() ← 布局阶段                   │
│         ↓                                                        │
│  area.cc: ED_region_header_draw() ← 绘制阶段                     │
│         ↓                                                        │
│  interface.cc: block_draw() ← 遍历所有按钮                       │
│         ↓                                                        │
│  interface_widgets.cc: draw_button() ← 选择 widget 样式          │
│         ↓                                                        │
│  interface_widgets.cc: widget_menubut() ← 圆角背景 + 箭头        │
│         ↓                                                        │
│  interface_widgets.cc: widgetbase_draw() ← GPU 批量绘制          │
│         ↓                                                        │
│  interface_widgets.cc: widget_draw_text_icon() ← 文本 + 图标     │
│                           ↓                                      │
│                   屏幕像素渲染                                    │
│                                                                   │
│  点击后 → interface_handlers.cc: block_open_begin()              │
│            → interface_region_menu_popup.cc: popup_menu_create() │
│            → 递归执行 menutype_draw() 绘制下拉内容               │
│            → 独立 UI block 渲染弹出菜单                          │
└─────────────────────────────────────────────────────────────────┘
```

---

## 关键技术要点

1. **双模式支持**：Menubar 支持展开（多个独立按钮）和折叠（单个汉堡菜单按钮）两种模式，通过 `draw_collapsible()` 动态切换。

2. **Python/C++ 混合架构**：菜单内容和布局用 Python 定义（灵活、易修改），底层渲染用 C++ 实现（性能关键）。

3. **按钮类型转换**：Header 区域的菜单按钮在创建时是 `Pulldown` 类型，随后通过 `button_type_set_menu_from_pulldown()` 转换为 `Menu` 类型，影响后续的交互行为和弹出菜单样式。

4. **GPU 批量绘制**：`widgetbase_draw()` 使用 GPU 批量绘制提高渲染效率，避免逐个绘制调用。

5. **BLF 字体渲染**：文本通过 Blender 自有的 BLF（Blender Font）库进行字形渲染，支持字体样式、对齐和 alpha 混合。

6. **独立弹出块**：下拉菜单作为独立的 UI block 渲染，拥有独立的布局空间和事件处理，不影响主 menubar 的按钮状态。

---

# Blender Icon 绘制流程分析

## 概述

Blender 的图标系统没有使用传统的纹理图集（Texture Atlas）方式，而是采用多种渲染策略：SVG 图标通过 SDF（Signed Distance Field）字体缓存渲染、栅格图标通过 Immediate Mode 逐帧上传像素纹理、矢量图标通过 C 函数指针直接发射 GPU 几何体。整个系统涉及 4 个主要层次。

---

## 核心文件清单

### Icon 枚举与类型定义
- [UI_resources.hh](source/blender/editors/include/UI_resources.hh)（第 27-36 行）— 定义 `BIFIconID` 类型和 `DEF_ICON` 宏系统
- [UI_icons.hh](source/blender/editors/include/UI_icons.hh)（第 41-1186 行）— 使用 `DEF_ICON` 宏列出所有内置图标
- [UI_interface_icons.hh](source/blender/editors/include/UI_interface_icons.hh) — `IconTextOverlay` 结构体、`DrawInfo` 类型常量

### Icon 数据存储（BKE 层）
- [BKE_icons.hh](source/blender/blenkernel/BKE_icons.hh)（第 28-62 行）— 定义底层 `Icon` 结构体
- [icons.cc](source/blender/blenkernel/intern/icons.cc) — 全局 `Map<int, Icon*>` 存储、`BKE_icon_get/set/init/ensure` API

### Icon 绘制核心（Editor 层）
- [interface_icons.cc](source/blender/editors/interface/interface_icons.cc) — `icontypes[]` 数组、`init_internal_icons()`、`icon_draw_size()`（第 1637 行）、`icon_draw_ex()`（第 2255 行）、`icon_draw_rect()`（第 1385 行）、`icon_get_theme_color()`（第 1094 行）

### Icon 在 Widget 中的绘制
- [interface_widgets.cc](source/blender/editors/interface/interface_widgets.cc) — `draw_button()`（第 5398 行）、`widget_draw_text_icon()`（第 2738 行）、`widget_draw_icon()`（第 1335 行）

### 事件图标
- [interface_icons_event.cc](source/blender/editors/interface/interface_icons_event.cc) — 键盘/鼠标事件图标的特殊绘制

### SVG 图标数据
- [datafiles/CMakeLists.txt](source/blender/editors/datafiles/CMakeLists.txt)（第 1014-1042 行）— 编译时 SVG → C 字节数组的转换
- 生成的 `svg_icons.cc` / `svg_icons.h` — 嵌入的 SVG 字节数据

---

## Icon 数据来源

### 1. 枚举定义（宏系统）

[UI_resources.hh:27](source/blender/editors/include/UI_resources.hh#L27) 中定义了宏系统，将 [UI_icons.hh](source/blender/editors/include/UI_icons.hh#L41) 的内容包含在 enum 体内：

```cpp
#define DEF_ICON(name) ICON_##name,
#define DEF_ICON_VECTOR(name) ICON_##name,
#define DEF_ICON_COLOR(name) ICON_##name,

enum BIFIconID_Static {
#include "UI_icons.hh"
  BIFICONID_LAST_STATIC,
};
using BIFIconID = int;
```

`UI_icons.hh` 中的每一行 `DEF_ICON(FOO)` 展开为 `ICON_FOO,` 枚举值。静态图标的 ID 范围是 `0`（`ICON_NONE`）到 `BIFICONID_LAST_STATIC - 1`，动态图标（预览、加载的文件）使用 `>= BIFICONID_LAST_STATIC` 的 ID。

### 2. 图标类型分类

`UI_icons.hh` 中定义了多种类型的图标宏：

| 宏 | 说明 |
|---|------|
| `DEF_ICON(name)` | 标准单色 SVG 图标 |
| `DEF_ICON_COLOR(name)` | 多色 SVG 图标 |
| `DEF_ICON_SCENE/COLLECTION/OBJECT/...` | 单色 SVG 带主题颜色覆盖 |
| `DEF_ICON_VECTOR(name)` | 程序化绘制的矢量图标（GPU 调用） |
| `DEF_ICON_BLANK(name)` | 空白占位图标 |

---

## Icon 运行时数据结构

### BKE 层：`Icon` 结构体

[BKE_icons.hh:28](source/blender/blenkernel/BKE_icons.hh#L28) 定义了底层图标存储：

```cpp
struct Icon {
  void *drawinfo;      // 指向 DrawInfo（绘制信息）
  void *obj;           // ID*, ImBuf*, PreviewImage*, Icon_Geom*, bGPDlayer*, StudioLight*
  char obj_type;       // ICON_DATA_ID, ICON_DATA_IMBUF, ICON_DATA_PREVIEW,
                       // ICON_DATA_GEOM, ICON_DATA_STUDIOLIGHT, ICON_DATA_GPLAYER
  char flag;
  short id_type;
  DrawInfoFreeFP drawinfo_free;
};
```

### Editor 层：`DrawInfo` 结构体

[interface_icons.cc:87](source/blender/editors/interface/interface_icons.cc#L87) 定义了决定实际绘制方式的结构体：

```cpp
struct DrawInfo {
  int type;  // ICON_TYPE_PREVIEW, ICON_TYPE_SVG_COLOR, ICON_TYPE_SVG_MONO,
             // ICON_TYPE_BUFFER, ICON_TYPE_IMBUF, ICON_TYPE_VECTOR,
             // ICON_TYPE_GEOM, ICON_TYPE_EVENT, ICON_TYPE_GPLAYER, ICON_TYPE_BLANK
  union {
    struct { VectorDrawFunc func; } vector;           // 矢量绘制的函数指针
    struct { ImBuf *image_cache; bool inverted; } geom; // 几何体光栅化缓存
    struct { IconImage *image; } buffer;               // 缓冲像素数据
    struct { int theme_color; } texture;               // SVG 图标主题颜色键
    struct { short event_type; short event_value; int icon; DrawInfo *next; } input;
  } data;
};
```

### `icontypes[]` 数组 — 枚举到类型的桥梁

[interface_icons.cc:123](source/blender/editors/interface/interface_icons.cc#L123)：

```cpp
static const IconType icontypes[] = {
#  define DEF_ICON(name) {ICON_TYPE_SVG_MONO, 0},
#  define DEF_ICON_COLOR(name) {ICON_TYPE_SVG_COLOR, 0},
#  define DEF_ICON_SCENE(name) {ICON_TYPE_SVG_MONO, TH_ICON_SCENE},
#  define DEF_ICON_COLLECTION(name) {ICON_TYPE_SVG_MONO, TH_ICON_COLLECTION},
#  define DEF_ICON_VECTOR(name) {ICON_TYPE_VECTOR, 0},
#  define DEF_ICON_BLANK(name) {ICON_TYPE_BLANK, 0},
   // ... 更多 ...
#  include "UI_icons.hh"
};
```

数组索引 = `icon_id`，每个元素包含 icon 类型和可选的主题颜色键。

---

## 完整绘制流程

### 第 1 步：按钮绘制入口

[interface_widgets.cc:5398](source/blender/editors/interface/interface_widgets.cc#L5398) — `draw_button()` 是每个按钮绘制的主函数。在完成 widget 背景绘制后，通过 `wt->text()` 分发到文本/图标绘制：

```cpp
if (wt->text) {
  wt->text(fstyle, &wt->wcol, but, rect);  // 第 5760 行
}
```

---

### 第 2 步：文本+图标绘制

[interface_widgets.cc:2738](source/blender/editors/interface/interface_widgets.cc#L2738) — `widget_draw_text_icon()`：

处理所有文本和图标组合情况：
- 预览图标 + 下方标签（第 2775 行）
- 左侧图标 + 右侧文本（第 2806 行）
- 纯文本无图标

在第 2847 行调用图标绘制：
```cpp
widget_draw_icon(but, icon, alpha, rect, icon_color);
```

---

### 第 3 步：单按钮图标绘制

[interface_widgets.cc:1335](source/blender/editors/interface/interface_widgets.cc#L1335) — `widget_draw_icon()`：

1. 检查是否为预览图标（`BUT_ICON_PREVIEW` 标志）→ 委托给 `widget_draw_preview_icon()`
2. 根据按钮类型调整 alpha（Toggle/Row 按钮 0.75、Labels 有自己的因子）
3. 确定图标位置：左对齐或居中
4. 在 zoom ≈ 1.0 时对坐标进行整数舍入，确保清晰渲染（第 1421-1425 行）
5. 通过 `icon_get_theme_color()` 解析颜色（第 1431 行）
6. 应用 hover/select 状态效果到 alpha
7. 调用 `icon_draw_ex()` 传入所有计算好的参数

**像素对齐逻辑**（第 1418-1425 行）：
```cpp
if (aspect > 0.95f && aspect < 1.05f) {
  xs = roundf(xs);
  ys = roundf(ys);
}
```

---

### 第 4 步：公共绘制 API

[interface_icons.cc:2255](source/blender/editors/interface/interface_icons.cc#L2255) — `icon_draw_ex()`：

```cpp
void icon_draw_ex(float x, float y, int icon_id, float aspect, float alpha,
                  float desaturate, const uchar mono_color[4],
                  bool mono_border, const IconTextOverlay *text_overlay,
                  bool inverted)
```

这是一个轻量包装，调用 `icon_draw_size()` 并传入 `ICON_SIZE_ICON`（默认 16 像素）。

便捷包装函数：
- `icon_draw()`（第 2228 行）— 默认 alpha/DPI
- `icon_draw_alpha()`（第 2234 行）— 自定义 alpha
- `icon_draw_preview()`（第 2240 行）— 预览图指定尺寸

---

### 第 5 步：核心绘制调度

[interface_icons.cc:1637](source/blender/editors/interface/interface_icons.cc#L1637) — `icon_draw_size()`：

这是整个图标系统的核心渲染调度中心：

1. 通过 `BKE_icon_get(icon_id)` 查找 `Icon*`（第 1657 行），失败则回退到 `ICON_NOT_FOUND`
2. 应用全局图标亮度 `btheme->tui.icon_alpha`（第 1668 行）
3. 调用 `icon_ensure_drawinfo(icon)` 获取/构建 `DrawInfo`（第 1676 行）
4. 根据 `di->type` 分派到不同的绘制方法：

| `di->type` | 绘制方法 | 说明 |
|---|---|---|
| `ICON_TYPE_SVG_MONO` | `BLF_draw_svg_icon()` | 单色 SVG 用 `mono_color` 着色 |
| `ICON_TYPE_SVG_COLOR` | `BLF_draw_svg_icon()` | 多色 SVG 保留自身颜色 |
| `ICON_TYPE_VECTOR` | `di->data.vector.func()` | 调用 C 函数指针，发射 GPU 几何体 |
| `ICON_TYPE_GEOM` | 光栅化 → `icon_draw_rect()` | 先光栅化到 ImBuf 缓存，再绘制 |
| `ICON_TYPE_BUFFER` | 延迟加载 → `icon_draw_rect()` | 从嵌入数据解压 PNG，再绘制 |
| `ICON_TYPE_IMBUF` | `icon_draw_rect()` | 直接拿 `ibuf->byte_data` 绘制 |
| `ICON_TYPE_PREVIEW` | `icon_draw_rect()` | 从 PreviewImage 获取对应尺寸的像素 |
| `ICON_TYPE_EVENT` | `icon_draw_rect_input()` | 键盘/鼠标事件图标 |
| `ICON_TYPE_GPLAYER` | Grease Pencil 图层颜色 | 使用特定图层颜色 |

---

### 第 6 步：SVG 图标绘制（BLF 系统）

对于 `ICON_TYPE_SVG_MONO` 和 `ICON_TYPE_SVG_COLOR` 图标：

- SVG 数据在编译时从 `release/datafiles/icons_svg/*.svg` 转换为 C 字节数组
- **不是传统的纹理图集**：Blender 使用 BLF（Blender Font）系统的 SDF（Signed Distance Field）字体缓存技术
- 每个 SVG 图标被当作字体的一个"字形"处理，缓存在 BLF 的 SDF 纹理中
- 首次绘制一个图标时会生成其 SDF 表示并缓存；后续绘制直接使用缓存
- 单色 SVG：用传入的 `mono_color` 参数着色（通常来自主题颜色）
- 多色 SVG：保留自身的多种颜色，但 alpha 通道会被调制
- 可选轮廓渲染：通过 `outline_alpha` 参数控制

[interface_icons.cc:1756](source/blender/editors/interface/interface_icons.cc#L1756) — SVG 单色绘制调用：
```cpp
BLF_draw_svg_icon(icon_id, x, y, w, h, mono_color, outline_alpha, ...);
```

---

### 第 7 步：栅格图标绘制（Immediate Mode）

[interface_icons.cc:1385](source/blender/editors/interface/interface_icons.cc#L1385) — `icon_draw_rect()`：

对于 `ICON_TYPE_BUFFER`、`ICON_TYPE_IMBUF`、`ICON_TYPE_PREVIEW`、`ICON_TYPE_GEOM` 等栅格图标：

1. 计算绘制位置/大小，保持纵横比缩放
2. 选择 GPU 着色器：
   - `GPU_SHADER_3D_IMAGE_COLOR`：标准绘制
   - `GPU_SHADER_2D_IMAGE_DESATURATE_COLOR`：去饱和度绘制
3. 调用 `immDrawPixelsTexScaledFullSize()` — Blender 的 Immediate Mode 像素纹理绘制函数
4. **每帧上传**像素数据作为临时纹理（无持久化图集缓存）

---

### 第 8 步：矢量图标绘制（GPU 几何体）

对于 `ICON_TYPE_VECTOR` 图标（如关键帧形状、颜色集、套接字形状等）：

- 通过 C 函数指针 `di->data.vector.func()` 直接发射 GPU 几何体
- 不需要任何纹理
- 典型例子：`ICON_KEYFRAME`、`ICON_KEYFRAME_HLT` 等关键帧图标

---

## Icon 查找与注册

### 全局注册中心

[icons.cc](source/blender/blenkernel/intern/icons.cc) — 全局 `Map<int, Icon*>` 存储所有图标，由互斥锁保护：

```cpp
static GlobalIconsMap &get_global_icons_map() { ... }
```

API 函数：
- `BKE_icon_set(int icon_id, Icon *icon)` — 注册图标
- `BKE_icon_get(int icon_id)` — 通过 ID 查找图标
- `BKE_icons_init(int first_dyn_id)` — 初始化，`first_dyn_id = BIFICONID_LAST_STATIC`
- `BKE_icon_id_ensure(ID *id)` — 获取或创建数据块预览图标
- `BKE_icon_imbuf_create(ImBuf*)` — 从图像缓冲区创建图标

### 静态图标初始化

[interface_icons.cc:1105](source/blender/editors/interface/interface_icons.cc#L1105) — `icons_init()` 在 Blender 启动时被调用：

1. `init_internal_icons()`（第 919 行）— 注册所有 SVG 图标
   - `def_internal_icon()`（第 141 行）创建 SVG 单色/彩色图标
   - `def_internal_vicon()`（第 192 行）创建矢量图标（带函数指针）
2. `init_event_icons()` — 注册事件/按键图标

### DrawInfo 延迟创建

`icon_ensure_drawinfo(Icon *icon)`（第 1083 行）按需从 BKE 层 `Icon` 创建 `DrawInfo`：

```cpp
static DrawInfo *icon_create_drawinfo(Icon *icon) {
  if (obj_type == ICON_DATA_ID || obj_type == ICON_DATA_PREVIEW)  -> ICON_TYPE_PREVIEW
  if (obj_type == ICON_DATA_IMBUF)                                -> ICON_TYPE_IMBUF
  if (obj_type == ICON_DATA_GEOM)                                 -> ICON_TYPE_GEOM
  if (obj_type == ICON_DATA_STUDIOLIGHT)                          -> ICON_TYPE_BUFFER
  if (obj_type == ICON_DATA_GPLAYER)                              -> ICON_TYPE_GPLAYER
}
```

### 图标转换函数

[interface_icons.cc](source/blender/editors/interface/interface_icons.cc) 中的映射函数：

- `icon_from_idcode(int idcode)`（第 2052 行）— 数据块类型 → 图标（如 `ID_ME` → `ICON_MESH_DATA`）
- `icon_from_object_type(const Object*)`（第 2167 行）— 对象类型 → 图标
- `icon_from_object_mode(int mode)`（第 2137 行）— 编辑模式 → 图标（如 `OB_MODE_EDIT` → `ICON_EDITMODE_HLT`）
- `icon_from_library(const ID*)`（第 1966 行）— 返回库状态图标（链接、间接、覆盖等）
- `icon_from_rnaptr(const bContext*, PointerRNA*, int rnaicon, bool big)`（第 1995 行）— 从 RNA 指针解析图标

---

## Icon 颜色与主题

### 主题颜色应用

[interface_icons.cc:1094](source/blender/editors/interface/interface_icons.cc#L1094) — `icon_get_theme_color()`：

```cpp
bool icon_get_theme_color(int icon_id, uchar color[4]) {
  Icon *icon = BKE_icon_get(icon_id);
  DrawInfo *di = icon_ensure_drawinfo(icon);
  return theme::get_icon_color_4ubv(di->data.texture.theme_color, color);
}
```

`theme_color` 值来自 `icontypes[]` 数组。例如：
- `DEF_ICON_SCENE` → `TH_ICON_SCENE`（场景主题色）
- `DEF_ICON_OBJECT` → `TH_ICON_OBJECT`（对象主题色）
- `DEF_ICON_MODIFIER` → `TH_ICON_MODIFIER`（修改器主题色）
- 标准的 `DEF_ICON` → `0`（无主题覆盖，使用文本颜色）

在 `widget_draw_icon()` 中（第 1431 行）：
```cpp
const bool has_theme = !but->col[3] && icon_get_theme_color(int(icon), color);
```
如果按钮有显式颜色覆盖（`but->col[3] != 0`），优先使用按钮颜色，否则使用主题颜色。

### Alpha 调制链

在 `icon_draw_size()` 中：
```cpp
alpha *= btheme->tui.icon_alpha;  // 全局图标亮度设置
```

在 `widget_draw_icon()` 中进一步调制：
- **Toggle/Row 按钮**（未选中且未 hover）→ alpha = 0.75
- **Disabled 按钮** → 颜色通过 `widget_color_disabled()` 转换为禁用色
- **图标不匹配按钮区域** → `alpha *= max(width/height, 0.0f)` 渐变消失
- **有主题色的非 hover 图标** → 额外的 0.8x alpha
- **工具栏项** → 使用去饱和度：`1.0f - btheme->tui.icon_saturation`

---

## Geom 图标缓存

对于 `ICON_TYPE_GEOM` 图标（基于几何体的矢量图标，如某些复杂形状）：

[interface_icons.cc:1712](source/blender/editors/interface/interface_icons.cc#L1712) — `icon_draw_size()` 将光栅化结果缓存在 `di->data.geom.image_cache` 中：

- 如果尺寸或主题反转变化，缓存通过 `BKE_icon_geom_rasterize()` 重新生成
- 缓存的是 `ImBuf` 格式的像素数据

---

## Buffer 图标延迟加载

对于 `ICON_TYPE_BUFFER` 图标（工作室光照、Matcap 等）：

[interface_icons.cc:899](source/blender/editors/interface/interface_icons.cc#L899) — `icon_verify_datatoc()`：

- 可能在后台线程通过 `wmJob` 生成
- 处理嵌入 PNG 数据的延迟解压
- 解压后的数据存入 `IconImage::rect` 缓冲区

---

## 图标尺寸系统

[UI_interface_icons.hh](source/blender/editors/include/UI_interface_icons.hh)：

```cpp
#define ICON_DEFAULT_HEIGHT 16
#define ICON_DEFAULT_WIDTH  16
```

绘制时实际像素大小由 `aspect` 比率（UI 缩放的反比）计算：

```cpp
int w = int(fdraw_size / aspect + 0.5f);
int h = int(fdraw_size / aspect + 0.5f);
```

---

## 完整流水线（从 icon_id 到像素）

```
icon_id (BIFIconID / int)
         |
         ▼
BKE_icon_get(icon_id)
         |  全局 Map<int, Icon*> 查找
         ▼
Icon*  (obj_type, obj, drawinfo)
         |
         ▼
icon_ensure_drawinfo(icon)
         |  延迟创建 DrawInfo
         ▼
DrawInfo*  (type: SVG_MONO|SVG_COLOR|VECTOR|BUFFER|IMBUF|PREVIEW|GEOM|EVENT|GPLAYER)
         |
         ▼
icon_draw_size() 分发：

  ┌─ SVG_MONO / SVG_COLOR ──► BLF_draw_svg_icon()
  │                            (BLF SDF 字体缓存)
  │
  ├─ VECTOR ──► di->data.vector.func(x, y, w, h, alpha, color)
  │              (GPU 几何体直接发射)
  │
  ├─ BUFFER ──► icon_verify_datatoc() [延迟解压 PNG]
  │              → icon_draw_rect()
  │                → immDrawPixelsTexScaledFullSize()
  │                  (GPU_SHADER_3D_IMAGE_COLOR 或 _DESATURATE)
  │
  ├─ IMBUF ──► icon_draw_rect(ibuf->byte_data())
  │              → immDrawPixelsTexScaledFullSize()
  │
  ├─ PREVIEW ──► icon_draw_rect(PreviewImage rect[size])
  │               → immDrawPixelsTexScaledFullSize()
  │
  ├─ GEOM ──► BKE_icon_geom_rasterize() [光栅化]
  │            → 缓存 ImBuf → icon_draw_rect()
  │              → immDrawPixelsTexScaledFullSize()
  │
  ├─ EVENT ──► icon_draw_rect_input()
  │
  └─ GPLAYER / BLANK ──► 特殊处理/跳过
```

---

## 从按钮角度的完整调用链

```
draw_button()                                          [interface_widgets.cc:5398]
  └─► wt->text() = widget_draw_text_icon()             [interface_widgets.cc:2738]
        └─► widget_draw_icon(but, icon, alpha, rect, color)  [interface_widgets.cc:1335]
              └─► icon_draw_ex(x, y, icon_id, aspect, alpha, ...)  [interface_icons.cc:2255]
                    └─► icon_draw_size(x, y, icon_id, aspect, ...)   [interface_icons.cc:1637]
                          └─► [根据 DrawInfo.type 分派到上述 8 种绘制路径]
```

---

## 关键技术要点

1. **无传统纹理图集**：Blender 不使用打包的精灵图（sprite sheet）。SVG 图标通过 BLF SDF 字体缓存渲染，栅格图标每帧通过 Immediate Mode 上传像素数据。

2. **多种渲染策略共存**：同一个 `icon_draw_size()` 根据 `DrawInfo.type` 分派到 8 种不同的绘制路径，适应不同类型图标的性能需求。

3. **编译时 SVG 嵌入**：SVG 图标在编译时转换为 C 字节数组并链接到 `blenfont` 库，通过 `BLF_draw_svg_icon()` 访问。

4. **SDF 缓存**：BLF 系统使用 Signed Distance Field 技术缓存图标字形，首次渲染后后续帧可直接命中缓存，效率高且支持任意缩放。

5. **延迟创建与加载**：`DrawInfo` 延迟按需创建；Buffer 图标支持后台线程加载，不会阻塞 UI。

6. **主题色集成**：图标颜色与 Blender 主题系统深度集成，不同类型的图标可以自动使用对应主题颜色（场景色、对象色等），支持快捷键亮度调节。

7. **像素对齐**：在 1x 缩放时对图标坐标进行整数舍入，确保最小化模糊。

8. **多级 Alpha 调制**：图标的透明度经过按钮状态（选中、hover、禁用）、全局设置、主题配置的多级调制链。
