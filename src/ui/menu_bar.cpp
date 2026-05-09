#include "menu_bar.h"
#include "../render/theme.h"
#include <iostream>

namespace bl_ui {

using namespace Theme;

void MenuBar::set_dependencies(Roundbox* rb, Font* font, MenuRegistry* reg, IconAtlas* icons) {
    _rb    = rb;
    _font  = font;
    _reg   = reg;
    _icons = icons;
}

void MenuBar::add_menu(std::string_view label, std::string_view menu_idname) {
    _items.push_back({std::string(label), std::string(menu_idname)});
}

// ---------------------------------------------------------------------------
// Layout — compute x positions for each bar item
// ---------------------------------------------------------------------------

void MenuBar::_layout(float /*vp_w*/) {
    float cx = HEADER_PAD_X;
    for (auto& item : _items) {
        item.w = _font->measure_text(item.label) + HEADER_PAD_X * 2.f;
        item.x = cx;
        cx += item.w;
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void MenuBar::draw(float vp_w, float vp_h) {
    if (!_rb || !_font) return;
    _vp_w = vp_w;
    _vp_h = vp_h;

    _layout(vp_w);

    // Full-width header bar background
    _rb->draw_rect_filled(0.f, 0.f, vp_w, HEADER_H, HEADER_BG);

    for (int i = 0; i < static_cast<int>(_items.size()); ++i) {
        const auto& item = _items[i];
        bool active  = (i == _active);
        bool hovered = (i == _hovered);

        if (active || hovered) {
            _rb->draw_roundbox(item.x, 1.f, item.w, HEADER_H - 2.f,
                               MENU_RADIUS, HEADER_HOVER);
        }

        RGBA tc = (active || hovered) ? ITEM_TEXT_SEL : HEADER_TEXT;
        float ty = (HEADER_H - _font->line_height()) * 0.5f;
        _font->draw_text(item.label,
                         item.x + HEADER_PAD_X,
                         ty, tc);
    }

    // Draw open popup
    if (_popup) {
        _popup->draw();

        // Check if popup closed or consumed an operator
        if (_popup->is_closed()) {
            if (_popup->consumed_operator() && _op_cb) {
                _op_cb(*_popup->consumed_operator());
            }
            _close_popup();
        }
    }
    (void)vp_h;
}

// ---------------------------------------------------------------------------
// Popup management
// ---------------------------------------------------------------------------

void MenuBar::_open_popup(int idx, float vp_w, float vp_h) {
    if (idx < 0 || idx >= static_cast<int>(_items.size())) return;
    const auto& item = _items[idx];
    MenuType* mt = _reg->find(item.menu_idname, true);
    if (!mt) {
        std::cerr << "[bl_ui] MenuBar: menu not found: " << item.menu_idname << "\n";
        return;
    }
    _active = idx;
    _popup = std::make_unique<PopupMenu>(
        mt, _ctx,
        item.x,       // anchor x = left edge of bar button
        HEADER_H,     // anchor y = bottom of header bar
        vp_w, vp_h,
        _reg, _rb, _font, _icons
    );
}

void MenuBar::_close_popup() {
    _popup.reset();
    _active = -1;
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void MenuBar::handle_mouse_move(float mx, float my) {
    _mx = mx;
    _my = my;

    // Update hover state in bar items
    _hovered = -1;
    if (my >= 0.f && my <= HEADER_H) {
        for (int i = 0; i < static_cast<int>(_items.size()); ++i) {
            const auto& item = _items[i];
            if (mx >= item.x && mx < item.x + item.w) {
                _hovered = i;
                // If a menu is already open, switch to the hovered item's menu
                if (_popup && i != _active) {
                    _close_popup();
                    _open_popup(i, _vp_w, _vp_h);
                }
                break;
            }
        }
    }

    // Forward move to popup
    if (_popup) {
        _popup->handle_mouse(mx, my, false);
        if (_popup->is_closed()) {
            if (_popup->consumed_operator() && _op_cb) {
                _op_cb(*_popup->consumed_operator());
            }
            _close_popup();
        }
    }
}

void MenuBar::handle_mouse_button(float mx, float my, bool pressed, bool released) {
    (void)released;
    if (!pressed) return;

    // Check if click is on a bar item
    if (my >= 0.f && my <= HEADER_H) {
        for (int i = 0; i < static_cast<int>(_items.size()); ++i) {
            const auto& item = _items[i];
            if (mx >= item.x && mx < item.x + item.w) {
                if (_active == i) {
                    _close_popup(); // toggle off
                } else {
                    _close_popup();
                    _open_popup(i, _vp_w, _vp_h);
                }
                return;
            }
        }
    }

    // Forward click to open popup
    if (_popup) {
        _popup->handle_mouse(mx, my, true);
        if (_popup->is_closed()) {
            if (_popup->consumed_operator() && _op_cb) {
                _op_cb(*_popup->consumed_operator());
            }
            _close_popup();
        }
    }
}

void MenuBar::handle_key(int glfw_key, int mods) {
    if (_popup) {
        _popup->handle_key(glfw_key, mods);
        if (_popup->is_closed()) {
            _close_popup();
        }
    }
}

} // namespace bl_ui
