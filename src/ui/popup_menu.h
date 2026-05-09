#pragma once
#include <bl_ui/menu_type.h>
#include "../render/roundbox.h"
#include "../render/font.h"
#include "../render/icon_atlas.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bl_ui {

// ---------------------------------------------------------------------------
// PopupMenu — dropdown popup widget
// Mirrors interface_region_menu_popup.cc
// ---------------------------------------------------------------------------

class PopupMenu {
public:
    PopupMenu(MenuType*      mt,
              const Context& ctx,
              float          anchor_x,   // left edge of trigger (screen px)
              float          anchor_y,   // top edge of popup  (screen px)
              float          vp_w,
              float          vp_h,
              MenuRegistry*  reg,
              Roundbox*      rb,
              Font*          font,
              IconAtlas*     icons = nullptr);

    // Draw everything (this popup + any open submenu).
    void draw();

    // Feed mouse position and click state.
    // Returns true if the event was consumed.
    bool handle_mouse(float mx, float my, bool clicked);

    // Feed a key event.  Returns true if consumed.
    bool handle_key(int glfw_key, int mods);

    bool is_closed()   const { return _closed; }

    // Returns the operator idname that was activated, if any.
    std::optional<std::string> consumed_operator() const { return _op_result; }

private:
    struct ItemRect {
        std::size_t item_idx;   // index into _items
        float       y0, y1;     // top/bottom bounds in screen px
        bool        clickable;
    };

    const std::vector<LayoutItem>& _items() const;
    void _measure();
    void _position(float vp_w, float vp_h);
    void _draw_item(const ItemRect& ir, bool hovered);
    void _open_submenu(int idx);
    void _close_submenu();

    std::vector<ItemRect> _item_rects;

    float _x = 0.f, _y = 0.f, _w = 0.f, _h = 0.f;
    float _vp_w = 800.f, _vp_h = 600.f;  // logical viewport (for submenu positioning)
    int   _hovered  = -1;
    bool  _closed   = false;

    std::optional<std::string>  _op_result;
    std::unique_ptr<PopupMenu> _submenu;

    // _menu owns the Layout; its items() are accessed via _menu.layout.items()
    Menu          _menu;
    MenuRegistry* _reg;
    Roundbox*     _rb;
    Font*         _font;
    IconAtlas*    _icons;
    Context       _ctx;
};

} // namespace bl_ui
