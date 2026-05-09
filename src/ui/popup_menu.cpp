#include "popup_menu.h"
#include "../render/theme.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace bl_ui {

using namespace Theme;

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
    float max_w = std::roundf(160.f * UI_SCALE);

    const float icon_w = (_icons && _icons->ready()) ? (_icons->icon_size() + 4.f) : 0.f;

    // widget_draw_submenu_tria: ICON_DEFAULT_HEIGHT(16) scaled by 0.4 → 6.4px.
    // UI_MENU_SUBMENU_PADDING (= ARROW_PAD_R = 6px) reserves the right-side space for it.
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
                // Right side: ARROW_PAD_R (= UI_MENU_SUBMENU_PADDING = 6px) + ITEM_PAD_X.
                // The 6.4px arrow fits within that margin.
                text_w = ITEM_PAD_X + icon_extra + _font->measure_text(label)
                         + ARROW_PAD_R + ITEM_PAD_X;
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
    _h = total_h + std::roundf(4.f * UI_SCALE); // 2px padding top + bottom
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
    float iy = _y + ir.y0 + std::roundf(2.f * UI_SCALE); // 2px top padding

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
            // widget_pulldownbut: only draws inner when UI_HOVER; uses wcol_pulldown.
            // inner_sel = {0xFF,0xFF,0xFF,0x1A} (HEADER_HOVER, 10% white).
            if (hovered) {
                _rb->draw_roundbox(_x + 2.f, iy, _w - 4.f, ITEM_HEIGHT,
                                   MENU_RADIUS, HEADER_HOVER);
            }
            // wcol_pulldown: text={0xD9,0xD9,0xD9}=HEADER_TEXT, text_sel=white.
            RGBA tc = hovered ? ITEM_TEXT_SEL : HEADER_TEXT;
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
            if (it.mnemonic != 0) {
                auto mpos = label.find(it.mnemonic);
                if (mpos != std::string_view::npos) {
                    float bw = _font->measure_text(label.substr(0, mpos));
                    float cw = _font->measure_text(label.substr(mpos, 1));
                    float ul_y = ty + _font->ascent() + 1.f;
                    _rb->draw_line_h(tx + bw, ul_y, cw, tc);
                }
            }
            // Shortcut: widget_draw_text_icon uses wcol->text * 0.5 alpha as hint.
            // On hover wcol->text becomes text_sel, so shortcut brightens too.
            if (!it.shortcut.empty()) {
                float sc_x = _x + _w - ITEM_PAD_X - _font->measure_text(it.shortcut);
                RGBA sc_c{tc.r, tc.g, tc.b, (unsigned char)(tc.a * 0.5f)};
                _font->draw_text(it.shortcut, sc_x, ty, sc_c);
            }

        } else if constexpr (std::is_same_v<Tp, MenuRefItem>) {
            // Same widget_pulldownbut / wcol_pulldown as OperatorItem — all popup
            // items use wcol_pulldown regardless of whether they open a submenu.
            if (hovered) {
                _rb->draw_roundbox(_x + 2.f, iy, _w - 4.f, ITEM_HEIGHT,
                                   MENU_RADIUS, HEADER_HOVER);
            }
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
            if (it.mnemonic != 0) {
                auto mpos = label.find(it.mnemonic);
                if (mpos != std::string::npos) {
                    float bw = _font->measure_text(std::string_view(label).substr(0, mpos));
                    float cw = _font->measure_text(std::string_view(label).substr(mpos, 1));
                    float ul_y = ty + _font->ascent() + 1.f;
                    _rb->draw_line_h(tx + bw, ul_y, cw, tc);
                }
            }
            // Submenu arrow — widget_draw_submenu_tria() / draw_anti_tria_rect().
            // Size: ICON_DEFAULT_HEIGHT(16) * 0.4 = 6.4px (BLI_rctf_scale(&r, 0.4f)).
            // Position: center_x = xmax - ICON_DEFAULT_HEIGHT/2 = xmax - 8px.
            // Color: wcol->text at 80% alpha (col[3] *= 0.8f).
            // ICON_DEFAULT_HEIGHT scales with UI_SCALE; arrow is 40% of that.
            const float ARROW_SZ = 16.f * UI_SCALE * 0.4f;
            RGBA arrow_c{tc.r, tc.g, tc.b, (unsigned char)(tc.a * 0.8f)};
            float ax = _x + _w - 8.f * UI_SCALE - ARROW_SZ * 0.5f; // center at xmax - 8*scale
            float ay = iy + (ITEM_HEIGHT - ARROW_SZ) * 0.5f;
            _rb->draw_triangle_right(ax, ay, ARROW_SZ, ARROW_SZ, arrow_c);
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
    float local_y = my - _y - std::roundf(2.f * UI_SCALE);
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

            const float pad_top = std::roundf(2.f * UI_SCALE);
            float sub_y = _y + pad_top;
            for (const auto& ir : _item_rects) {
                if (static_cast<int>(ir.item_idx) == idx) {
                    sub_y = _y + ir.y0 + pad_top;
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
