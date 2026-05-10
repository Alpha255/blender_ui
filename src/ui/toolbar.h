#pragma once
#include "../render/roundbox.h"
#include "../render/font.h"
#include "../render/icon_atlas.h"

namespace bl_ui {

// ---------------------------------------------------------------------------
// Toolbar — left-side floating panel with transform tool buttons.
//
// Matches Blender's T-panel (toolbar) in the 3D Viewport:
//   • Floating overlay panel on the left edge (does not resize the viewport).
//   • Four tools: Move / Rotate / Scale / Transform.
//   • Active tool highlighted in blue; hover effect on mouse over.
//   • Procedurally drawn icons (no SVG dependency).
//
// Input priority: left-button clicks over the panel are consumed; the
// viewport still receives middle-button (camera) events everywhere.
// ---------------------------------------------------------------------------

class Toolbar {
public:
    enum Tool {
        TOOL_MOVE = 0,
        TOOL_ROTATE,
        TOOL_SCALE,
        TOOL_TRANSFORM,
        TOOL_COUNT
    };

    void set_viewport(float vp_w, float vp_h);
    void set_icons(IconAtlas* icons) { _icons = icons; }

    // Render the toolbar overlay. Called every frame from App::run().
    void draw(float header_h, Roundbox* rb, Font* font);

    // Returns true when the cursor is over the panel (event consumed).
    // Always safe to call for cursor-pos events (pressed = released = false).
    bool handle_mouse(float mx, float my, bool pressed, bool released);

    Tool       active_tool() const { return _active; }
    IconAtlas* icons()      const { return _icons;  }
    // Right edge of the panel — useful if caller wants to offset the viewport.
    float panel_right() const { return _px + _pw; }

private:
    void _layout(float header_h);

    // Procedural icon drawing — each draws into a square of size `sz`
    // centered at (cx, cy) using the shared Roundbox.
    static void _icon_move     (Roundbox* rb, float cx, float cy, float sz, RGBA col);
    static void _icon_rotate   (Roundbox* rb, float cx, float cy, float sz, RGBA col);
    static void _icon_scale    (Roundbox* rb, float cx, float cy, float sz, RGBA col);
    static void _icon_transform(Roundbox* rb, float cx, float cy, float sz, RGBA col);

    // Draw one arrow arm: shaft from (origin + dir*sr) to (origin + dir*(tip-ah)),
    // then V-shaped arrowhead from tip.
    static void _arrow(Roundbox* rb,
                       float cx, float cy, float dx, float dy,
                       float sr, float tip,
                       float ah, float aw, float lw, RGBA col);

    float _vp_w = 800.f, _vp_h = 600.f;
    float _header_h = 26.f;

    // Panel bounding box (recomputed each frame)
    float _px = 0.f, _py = 0.f;
    float _pw = 0.f, _ph = 0.f;

    struct BtnRect { float x, y, w, h; };
    BtnRect _btns[TOOL_COUNT] = {};

    Tool       _active  = TOOL_MOVE;
    int        _hov_btn = -1;
    IconAtlas* _icons   = nullptr;
};

} // namespace bl_ui
