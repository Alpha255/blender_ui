#pragma once
#include <bl_ui/menu_type.h>
#include "../render/roundbox.h"
#include "../render/font.h"
#include "../render/icon_atlas.h"
#include "popup_menu.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace bl_ui {

// ---------------------------------------------------------------------------
// MenuBar — horizontal header bar with dropdown menus
// Mirrors ED_region_header_layout/draw + space_topbar.py
// Source: source/blender/editors/screen/area.cc:3852
// ---------------------------------------------------------------------------

class MenuBar {
public:
    MenuBar() = default;

    // Inject render dependencies
    void set_dependencies(Roundbox* rb, Font* font, MenuRegistry* reg,
                          IconAtlas* icons = nullptr);

    // Register a top-level menu entry (label shown in bar, menu_idname → registry)
    void add_menu(std::string_view label, std::string_view menu_idname);

    // Called each frame; vp_w/vp_h = current framebuffer size
    void draw(float vp_w, float vp_h);

    // Mouse events (screen coords, y=0 at top)
    void handle_mouse_move(float mx, float my);
    void handle_mouse_button(float mx, float my, bool pressed, bool released);

    // Key event (GLFW key code + mod flags)
    void handle_key(int glfw_key, int mods);

    // Operator execution callback
    void set_operator_callback(std::function<void(const std::string&)> cb) {
        _op_cb = std::move(cb);
    }

    bool has_open_popup() const { return _popup != nullptr; }

private:
    struct BarItem {
        std::string label;
        std::string menu_idname;
        float       x = 0.f, w = 0.f;
    };

    void _layout(float vp_w);
    void _open_popup(int idx, float vp_w, float vp_h);
    void _close_popup();

    std::vector<BarItem>        _items;
    int                         _active  = -1;  // index of open menu
    int                         _hovered = -1;

    std::unique_ptr<PopupMenu>  _popup;
    Context                     _ctx;

    Roundbox*     _rb    = nullptr;
    Font*         _font  = nullptr;
    MenuRegistry* _reg   = nullptr;
    IconAtlas*    _icons = nullptr;

    std::function<void(const std::string&)> _op_cb;

    // Mouse state and last known logical viewport size
    float _mx   = 0.f, _my   = 0.f;
    float _vp_w = 800.f, _vp_h = 600.f;
};

} // namespace bl_ui
