#pragma once
#include "../render/roundbox.h"
#include "../render/font.h"
#include <string>
#include <vector>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Checkbox — a vertical stack of toggle items matching Blender's option button.
//
// Visual style mirrors uiWidgetColors wcol_option (dark theme):
//   unchecked box : #2B2B2B fill, #171717 outline, 2px border radius
//   checked box   : #4872B3 fill (blue), same outline
//   checkmark     : white, Blender's g_shape_preset_checkmark_vert geometry
//   label text    : #E6E6E6 normal, #FFFFFF on hover row
//   hover row     : slightly lighter box outline (#646464)
//
// Source ref: source/blender/editors/interface/interface_widgets.cc:4840
//             widget_optionbut()
// ---------------------------------------------------------------------------

class Checkbox {
public:
    struct Item {
        std::string label;
        bool        checked = false;
    };

    // Must be called before draw() / handle_mouse().
    void set_dependencies(Roundbox* rb, Font* font);

    void set_viewport(float w, float h);

    // Draw the checkbox list starting at top-left (x, y).
    // mx, my — current cursor position (for hover highlighting).
    void draw(float x, float y, float mx, float my);

    // Returns true when an item's checked state changed.
    // Call on mouse press events (pressed=true on button-down).
    bool handle_mouse(float mx, float my, bool pressed);

    std::vector<Item>& items() { return _items; }
    const std::vector<Item>& items() const { return _items; }

private:
    // Per-item layout rect (recomputed in draw()).
    struct ItemRect {
        float x, y, w, h;   // full row bounding box
        float bx, by, bw, bh; // checkbox box only
    };

    void _build_rects(float x, float y);

    Roundbox* _rb   = nullptr;
    Font*     _font = nullptr;

    float _vp_w = 800.f, _vp_h = 600.f;

    // Current origin — set at the start of draw() and used by handle_mouse().
    float _ox = 0.f, _oy = 0.f;

    std::vector<Item>     _items;
    std::vector<ItemRect> _rects;  // parallel to _items, rebuilt each draw()

    int _hov = -1;  // hovered item index (-1 = none)
};

} // namespace bl_ui
