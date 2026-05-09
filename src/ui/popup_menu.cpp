#include "popup_menu.h"
#include "../render/theme.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace bl_ui {

using namespace Theme;

static constexpr float MIN_W = 160.f;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

PopupMenu::PopupMenu(MenuType*      mt,
                     const Context& ctx,
                     float          anchor_x,
                     float          anchor_y,
                     float          vp_w,
                     float          vp_h,
                     MenuRegistry*  reg,
                     Roundbox*      rb,
                     Font*          font,
                     IconAtlas*     icons)
    : _menu(mt->invoke_draw(ctx))
    , _reg(reg)
    , _rb(rb)
    , _font(font)
    , _icons(icons)
    , _ctx(ctx)
{
    _vp_w = vp_w;
    _vp_h = vp_h;
    _measure();
    _x = anchor_x;
    _y = anchor_y;
    _position(vp_w, vp_h);
}

// Shorthand to get items from the owned menu
const std::vector<LayoutItem>& PopupMenu::_items() const {
    return _menu.layout.items();
}

// ---------------------------------------------------------------------------
// Layout measurement
// ---------------------------------------------------------------------------

void PopupMenu::_measure() {
    const auto& items = _items();
    float max_w = MIN_W;

    const float icon_w = (_icons && _icons->ready()) ? (_icons->icon_size() + 4.f) : 0.f;

    // Arrow: Blender draws a geometric triangle via widget_draw_submenu_tria().
    // Blender: ICON_DEFAULT_HEIGHT=16px tall, width≈14px at 1× content_scale.
    static constexpr float ARROW_W = 14.f;

    for (const auto& item : items) {
        float text_w = 0.f;
        std::visit([&](auto&& it) {
            using Tp = std::decay_t<decltype(it)>;
            if constexpr (std::is_same_v<Tp, OperatorItem>) {
                float icon_extra = (it.icon_id != 0) ? icon_w : 0.f;
                float label_w = _font->measure_text(it.text.empty() ? it.idname : it.text);
                float sc_w = it.shortcut.empty() ? 0.f
                             : _font->measure_text(it.shortcut) + ITEM_PAD_X;
                text_w = ITEM_PAD_X + icon_extra + label_w + sc_w + ITEM_PAD_X;
            } else if constexpr (std::is_same_v<Tp, MenuRefItem>) {
                float icon_extra = (it.icon_id != 0) ? icon_w : 0.f;
                std::string label = it.text.empty() ? it.menu_idname : it.text;
                text_w = ITEM_PAD_X + icon_extra + _font->measure_text(label)
                         + ARROW_PAD_R + ARROW_W + ITEM_PAD_X;
            } else if constexpr (std::is_same_v<Tp, LabelItem>) {
                text_w = _font->measure_text(it.text) + ITEM_PAD_X * 2;
            }
        }, item);
        max_w = std::max(max_w, text_w);
    }
    _w = max_w;

    float total_h = 0.f;
    _item_rects.clear();

    for (std::size_t i = 0; i < items.size(); ++i) {
        float row_h = ITEM_HEIGHT;
        bool clickable = false;

        std::visit([&](auto&& it) {
            using Tp = std::decay_t<decltype(it)>;
            if constexpr (std::is_same_v<Tp, SeparatorItem>) {
                row_h = SEP_HEIGHT;
            } else if constexpr (std::is_same_v<Tp, OperatorItem> ||
                                  std::is_same_v<Tp, MenuRefItem>) {
                clickable = true;
            }
        }, items[i]);

        _item_rects.push_back({i, total_h, total_h + row_h, clickable});
        total_h += row_h;
    }
    _h = total_h + 4.f; // 2px padding top + bottom
}

// ---------------------------------------------------------------------------
// Positioning — clamp to viewport
// ---------------------------------------------------------------------------

void PopupMenu::_position(float vp_w, float vp_h) {
    if (_x + _w > vp_w) _x = vp_w - _w - 2.f;
    if (_x < 0.f)       _x = 2.f;
    if (_y + _h > vp_h) _y = vp_h - _h - 2.f;
    if (_y < 0.f)       _y = 2.f;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void PopupMenu::_draw_item(const ItemRect& ir, bool hovered) {
    const auto& items = _items();
    const LayoutItem& item = items[ir.item_idx];
    float iy = _y + ir.y0 + 2.f; // +2px top padding

    std::visit([&](auto&& it) {
        using Tp = std::decay_t<decltype(it)>;

        if constexpr (std::is_same_v<Tp, SeparatorItem>) {
            float mid = iy + SEP_HEIGHT * 0.5f;
            _rb->draw_line_h(_x + 4.f, mid, _w - 8.f, SEP_COLOR);

        } else if constexpr (std::is_same_v<Tp, LabelItem>) {
            _font->draw_text(it.text,
                             _x + ITEM_PAD_X,
                             iy + (ITEM_HEIGHT - _font->line_height()) * 0.5f,
                             ITEM_TEXT_DIM);

        } else if constexpr (std::is_same_v<Tp, OperatorItem>) {
            if (hovered) {
                _rb->draw_roundbox(_x + 2.f, iy, _w - 4.f, ITEM_HEIGHT,
                                   MENU_RADIUS, ITEM_HOVER);
            }
            RGBA tc = hovered ? ITEM_TEXT_SEL : ITEM_TEXT;
            float tx = _x + ITEM_PAD_X;
            if (_icons && _icons->ready() && it.icon_id != 0) {
                float isz = _icons->icon_size();
                float iy_icon = iy + (ITEM_HEIGHT - isz) * 0.5f;
                _icons->draw_icon(it.icon_id, tx, iy_icon, tc);
                tx += isz + 4.f;
            }
            float ty = iy + (ITEM_HEIGHT - _font->line_height()) * 0.5f;
            std::string_view label = it.text.empty() ? it.idname : it.text;
            _font->draw_text(label, tx, ty, tc);
            // Shortcut hint — right-aligned, always dimmed (wcol_menu_item.item)
            if (!it.shortcut.empty()) {
                float sc_x = _x + _w - ITEM_PAD_X - _font->measure_text(it.shortcut);
                _font->draw_text(it.shortcut, sc_x, ty, ITEM_SECONDARY);
            }

        } else if constexpr (std::is_same_v<Tp, MenuRefItem>) {
            if (hovered) {
                _rb->draw_roundbox(_x + 2.f, iy, _w - 4.f, ITEM_HEIGHT,
                                   MENU_RADIUS, HEADER_HOVER);
            }
            // wcol_pulldown: text = #D9D9D9, text_sel = #FFFFFF
            RGBA tc = hovered ? ITEM_TEXT_SEL : HEADER_TEXT;
            float tx = _x + ITEM_PAD_X;
            if (_icons && _icons->ready() && it.icon_id != 0) {
                float isz = _icons->icon_size();
                float iy_icon = iy + (ITEM_HEIGHT - isz) * 0.5f;
                _icons->draw_icon(it.icon_id, tx, iy_icon, tc);
                tx += isz + 4.f;
            }
            float ty = iy + (ITEM_HEIGHT - _font->line_height()) * 0.5f;
            std::string label = it.text.empty() ? it.menu_idname : it.text;
            _font->draw_text(label, tx, ty, tc);
            // Submenu arrow — geometric triangle matching Blender's
            // widget_draw_submenu_tria() / draw_anti_tria_rect().
            // Blender: ICON_DEFAULT_HEIGHT=16px, width≈14px at 1× content_scale.
            static constexpr float ARROW_H = 16.f * 0.5f;
            static constexpr float ARROW_W = 16.f * 0.5f;
            RGBA arrow_c = hovered ? ITEM_TEXT_SEL : ITEM_SECONDARY;
            float ax = _x + _w - ARROW_PAD_R - ARROW_W;
            float ay = iy + (ITEM_HEIGHT - ARROW_H) * 0.5f;
            _rb->draw_triangle_right(ax, ay, ARROW_W, ARROW_H, arrow_c);
        }
    }, item);
}

void PopupMenu::draw() {
    if (_closed) return;

    // Background + outline
    _rb->draw_roundbox(_x, _y, _w, _h,
                       MENU_RADIUS,
                       MENU_BG,
                       MENU_OUTLINE, 1.f);

    for (const auto& ir : _item_rects) {
        _draw_item(ir, static_cast<int>(ir.item_idx) == _hovered);
    }

    if (_submenu) _submenu->draw();
}

// ---------------------------------------------------------------------------
// Mouse handling
// ---------------------------------------------------------------------------

bool PopupMenu::handle_mouse(float mx, float my, bool clicked) {
    // Delegate to open submenu first
    if (_submenu) {
        if (_submenu->handle_mouse(mx, my, clicked)) {
            if (_submenu->is_closed()) {
                if (_submenu->consumed_operator()) {
                    _op_result = _submenu->consumed_operator();
                    _closed    = true;
                }
                _close_submenu();
            }
            return true;
        }
    }

    // Click outside → close
    if (clicked) {
        if (mx < _x || mx > _x + _w || my < _y || my > _y + _h) {
            _closed = true;
            return false;
        }
    }

    // Hit-test items
    _hovered = -1;
    float local_y = my - _y - 2.f;
    for (const auto& ir : _item_rects) {
        if (!ir.clickable) continue;
        if (local_y >= ir.y0 && local_y < ir.y1) {
            _hovered = static_cast<int>(ir.item_idx);
            break;
        }
    }

    if (clicked && _hovered >= 0) {
        const LayoutItem& item = _items()[_hovered];
        std::visit([&](auto&& it) {
            using Tp = std::decay_t<decltype(it)>;
            if constexpr (std::is_same_v<Tp, OperatorItem>) {
                _op_result = it.idname;
                _closed    = true;
            } else if constexpr (std::is_same_v<Tp, MenuRefItem>) {
                _close_submenu();
                _open_submenu(_hovered);
            }
        }, item);
        return true;
    }

    // Hover-open submenu for MenuRefItem
    if (_hovered >= 0) {
        const LayoutItem& item = _items()[_hovered];
        bool is_sub = std::visit([](auto&& it) {
            return std::is_same_v<std::decay_t<decltype(it)>, MenuRefItem>;
        }, item);
        if (is_sub) {
            if (!_submenu) _open_submenu(_hovered);
        } else {
            _close_submenu();
        }
    }

    return (mx >= _x && mx <= _x + _w && my >= _y && my <= _y + _h);
}

bool PopupMenu::handle_key(int glfw_key, int /*mods*/) {
    if (_submenu) {
        if (_submenu->handle_key(glfw_key, 0)) return true;
    }
    if (glfw_key == GLFW_KEY_ESCAPE) {
        _closed = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Submenu management
// ---------------------------------------------------------------------------

void PopupMenu::_open_submenu(int idx) {
    const LayoutItem& item = _items()[idx];
    std::visit([&](auto&& it) {
        using Tp = std::decay_t<decltype(it)>;
        if constexpr (std::is_same_v<Tp, MenuRefItem>) {
            MenuType* mt = _reg->find(it.menu_idname, true);
            if (!mt) return;

            float sub_y = _y + 2.f;
            for (const auto& ir : _item_rects) {
                if (static_cast<int>(ir.item_idx) == idx) {
                    sub_y = _y + ir.y0 + 2.f;
                    break;
                }
            }

            _submenu = std::make_unique<PopupMenu>(
                mt, _ctx,
                _x + _w,
                sub_y,
                _vp_w, _vp_h,
                _reg, _rb, _font, _icons
            );
        }
    }, item);
}

void PopupMenu::_close_submenu() {
    _submenu.reset();
}

} // namespace bl_ui
