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

const std::vector<LayoutItem>& PopupMenu::_items() const {
    return _menu.layout.items();
}

// ---------------------------------------------------------------------------
// Clip text to max_w pixels, inserting "..." in the middle when needed.
// Mirrors Blender's text_clip_middle_ex().
// ---------------------------------------------------------------------------

static std::string clip_text_middle(Font* font, const std::string& text, float max_w) {
    if (font->measure_text(text) <= max_w) return text;

    const std::string ellipsis = "...";
    float ell_w = font->measure_text(ellipsis);
    if (ell_w >= max_w) return ellipsis;

    float avail = max_w - ell_w;
    float half  = avail * 0.5f;

    // Find how many chars fit in the left half and right half.
    std::size_t left = 0;
    float acc = 0.f;
    while (left < text.size()) {
        float cw = font->measure_text(text.substr(left, 1));
        if (acc + cw > half) break;
        acc += cw;
        ++left;
    }

    std::size_t right = text.size();
    acc = 0.f;
    while (right > left) {
        float cw = font->measure_text(text.substr(right - 1, 1));
        if (acc + cw > half) break;
        acc += cw;
        --right;
    }

    return text.substr(0, left) + ellipsis + text.substr(right);
}

// ---------------------------------------------------------------------------
// Layout measurement
// ---------------------------------------------------------------------------

void PopupMenu::_measure() {
    const auto& items = _items();

    // Blender: padding = 0.25f * row_height (draw_menu_item, line 6171).
    // Icon column = row_height (one full widget_unit wide).
    const float padding  = 0.25f * ITEM_HEIGHT;
    const float icon_col = (_icons && _icons->ready()) ? ITEM_HEIGHT : 0.f;

    float max_w = std::roundf(160.f * UI_SCALE);

    for (const auto& item : items) {
        float text_w = 0.f;
        std::visit([&](auto&& it) {
            using Tp = std::decay_t<decltype(it)>;
            if constexpr (std::is_same_v<Tp, OperatorItem>) {
                float has_icon = (it.icon_id != 0) ? icon_col : 0.f;
                float label_w  = _font->measure_text(it.text.empty() ? it.idname : it.text);
                float sc_w     = it.shortcut.empty() ? 0.f
                                 : _font->measure_text(it.shortcut) + ITEM_PAD_X;
                // left: padding + icon_col + label; right: sc_w + padding
                text_w = padding + has_icon + label_w + sc_w + padding;
            } else if constexpr (std::is_same_v<Tp, MenuRefItem>) {
                float has_icon = (it.icon_id != 0) ? icon_col : 0.f;
                std::string label = it.text.empty() ? it.menu_idname : it.text;
                // right: ARROW_PAD_R (submenu triangle space) + padding
                text_w = padding + has_icon + _font->measure_text(label)
                         + ARROW_PAD_R + padding;
            } else if constexpr (std::is_same_v<Tp, LabelItem>) {
                text_w = padding + _font->measure_text(it.text) + padding;
            }
        }, item);
        max_w = std::max(max_w, text_w);
    }
    _w = max_w;

    float total_h = 0.f;
    _item_rects.clear();

    for (std::size_t i = 0; i < items.size(); ++i) {
        float row_h   = ITEM_HEIGHT;
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
    // 2px padding top + 2px padding bottom (Blender: UI_MENU_PADDING).
    _h = total_h + std::roundf(4.f * UI_SCALE);
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
// Draw a single menu item.
// Mirrors Blender's draw_menu_item() in interface_widgets.cc.
// ---------------------------------------------------------------------------

void PopupMenu::_draw_item(const ItemRect& ir, bool hovered) {
    const auto& items = _items();
    const LayoutItem& item = items[ir.item_idx];

    // Top padding: 2px (Blender: UI_MENU_PADDING / 2).
    const float pad_top  = std::roundf(2.f * UI_SCALE);
    const float iy       = _y + ir.y0 + pad_top;

    // Blender: padding = 0.25f * row_height (draw_menu_item, line 6171).
    const float padding  = 0.25f * ITEM_HEIGHT;

    // Highlight rect inset: widget_menu_itembut pads by 0.125 * widget_unit each side.
    const float hi_pad   = std::roundf(0.125f * ITEM_HEIGHT);

    std::visit([&](auto&& it) {
        using Tp = std::decay_t<decltype(it)>;

        if constexpr (std::is_same_v<Tp, SeparatorItem>) {
            // Blender draws a 1px line at vertical center of the separator row.
            float mid = iy + SEP_HEIGHT * 0.5f;
            _rb->draw_line_h(_x + 4.f, mid, _w - 8.f, SEP_COLOR);

        } else if constexpr (std::is_same_v<Tp, LabelItem>) {
            _font->draw_text(it.text,
                             _x + padding,
                             iy + (ITEM_HEIGHT - _font->line_height()) * 0.5f,
                             ITEM_TEXT_DIM);

        } else if constexpr (std::is_same_v<Tp, OperatorItem>) {
            // Background highlight — widget_menu_itembut with 0.125 * wu inset.
            if (hovered) {
                _rb->draw_roundbox(_x + hi_pad, iy, _w - 2.f * hi_pad, ITEM_HEIGHT,
                                   MENU_RADIUS, ITEM_HOVER);
            }

            // Text color: wcol_menu_item.text / text_sel.
            RGBA tc = hovered ? ITEM_TEXT_SEL : ITEM_TEXT;

            // Icon drawn at: xmin + 0.2 * UI_UNIT_X * zoom
            // (UI_UNIT_X = widget_unit = ITEM_HEIGHT in our system, zoom = 1.0).
            // draw_menu_item line 6253-6261.
            float tx = _x + padding;  // text start (no icon)
            if (_icons && _icons->ready() && it.icon_id != 0) {
                float icon_sz = _icons->icon_size();
                float ix = std::roundf(_x + 0.2f * ITEM_HEIGHT);
                float iy_icon = std::roundf(iy + 0.5f * (ITEM_HEIGHT - icon_sz));
                _icons->draw_icon(it.icon_id, ix, iy_icon, tc);
                // Text starts after the full icon column (ITEM_HEIGHT wide).
                tx = _x + padding + ITEM_HEIGHT;
            }

            float ty = iy + (ITEM_HEIGHT - _font->line_height()) * 0.5f;

            // Shortcut right-aligned (stored in it.shortcut, right of UI_SEP_CHAR).
            // Blender renders it grayed out (BUT_INACTIVE = 50% alpha).
            float sc_w = 0.f;
            if (!it.shortcut.empty()) {
                sc_w = _font->measure_text(it.shortcut) + ITEM_PAD_X;
            }

            // Available width for the label (shrink right end by shortcut + padding).
            float text_max_w = _w - (tx - _x) - sc_w - padding;
            std::string label = it.text.empty() ? it.idname : it.text;
            label = clip_text_middle(_font, label, text_max_w);
            _font->draw_text(label, tx, ty, tc);

            // Mnemonic underline.
            if (it.mnemonic != 0) {
                auto mpos = label.find(it.mnemonic);
                if (mpos != std::string::npos) {
                    float bw = _font->measure_text(label.substr(0, mpos));
                    float cw = _font->measure_text(label.substr(mpos, 1));
                    float ul_y = ty + _font->ascent() + 1.f;
                    _rb->draw_line_h(tx + bw, ul_y, cw, tc);
                }
            }

            // Shortcut at right edge, 50% alpha (Blender: BUT_INACTIVE → alpha * 0.5).
            if (!it.shortcut.empty()) {
                float sc_x = _x + _w - padding - _font->measure_text(it.shortcut);
                RGBA sc_c{tc.r, tc.g, tc.b, (unsigned char)(tc.a * 0.5f)};
                _font->draw_text(it.shortcut, sc_x, ty, sc_c);
            }

        } else if constexpr (std::is_same_v<Tp, MenuRefItem>) {
            // Same widget as OperatorItem — wcol_menu_item (popup context).
            if (hovered) {
                _rb->draw_roundbox(_x + hi_pad, iy, _w - 2.f * hi_pad, ITEM_HEIGHT,
                                   MENU_RADIUS, ITEM_HOVER);
            }
            RGBA tc = hovered ? ITEM_TEXT_SEL : ITEM_TEXT;

            float tx = _x + padding;
            if (_icons && _icons->ready() && it.icon_id != 0) {
                float icon_sz = _icons->icon_size();
                float ix = std::roundf(_x + 0.2f * ITEM_HEIGHT);
                float iy_icon = std::roundf(iy + 0.5f * (ITEM_HEIGHT - icon_sz));
                _icons->draw_icon(it.icon_id, ix, iy_icon, tc);
                tx = _x + padding + ITEM_HEIGHT;
            }

            float ty = iy + (ITEM_HEIGHT - _font->line_height()) * 0.5f;

            // Reserve right-side space for the submenu arrow (ARROW_PAD_R + padding).
            float text_max_w = _w - (tx - _x) - ARROW_PAD_R - padding;
            std::string label = it.text.empty() ? it.menu_idname : it.text;
            label = clip_text_middle(_font, label, text_max_w);
            _font->draw_text(label, tx, ty, tc);

            if (it.mnemonic != 0) {
                auto mpos = label.find(it.mnemonic);
                if (mpos != std::string::npos) {
                    float bw = _font->measure_text(label.substr(0, mpos));
                    float cw = _font->measure_text(label.substr(mpos, 1));
                    float ul_y = ty + _font->ascent() + 1.f;
                    _rb->draw_line_h(tx + bw, ul_y, cw, tc);
                }
            }

            // Submenu arrow — widget_draw_submenu_tria().
            // Blender: tria_height = ICON_DEFAULT_HEIGHT (16px scaled).
            //          tria_width  = ICON_DEFAULT_WIDTH  - 2 * pixelsize.
            //          BLI_rctf_scale(&r, 0.4f) → effective size ≈ 40% of icon size.
            //          Placed at xmax - tria_width, centred vertically.
            //          Color: wcol->text at 80% alpha.
            const float TRIA_H = 16.f * UI_SCALE * 0.4f;
            const float TRIA_W = (16.f * UI_SCALE - 2.f) * 0.4f;
            RGBA arrow_c{tc.r, tc.g, tc.b, (unsigned char)(tc.a * 0.8f)};
            float ax = _x + _w - padding - TRIA_W;
            float ay = iy + (ITEM_HEIGHT - TRIA_H) * 0.5f;
            _rb->draw_triangle_right(ax, ay, TRIA_W, TRIA_H, arrow_c);
        }
    }, item);
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void PopupMenu::draw() {
    if (_closed) return;

    // Soft drop shadow — widget_softshadow(rect, CNR_ALL, 0.25f * widget_unit).
    // Called before the background so it appears beneath.
    const float shadow_px = 0.25f * ITEM_HEIGHT;
    _rb->draw_softshadow(_x, _y, _w, _h, MENU_RADIUS, shadow_px);

    // Menu background + outline — wcol_menu_back.
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

    if (clicked) {
        if (mx < _x || mx > _x + _w || my < _y || my > _y + _h) {
            _closed = true;
            return false;
        }
    }

    _hovered = -1;
    const float pad_top  = std::roundf(2.f * UI_SCALE);
    float local_y = my - _y - pad_top;
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
