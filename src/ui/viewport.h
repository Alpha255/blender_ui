#pragma once
#include "../render/grid.h"

namespace bl_ui {

// ---------------------------------------------------------------------------
// Viewport2D — pannable, zoomable 2D viewport with an infinite grid.
//
// Coordinate conventions:
//   Screen space — y=0 at top, increasing downward, logical pixels.
//   World space  — x right, y downward (matches screen at zoom=1, pan=origin).
//   pan_x/pan_y  — screen position of world origin (0,0), logical pixels.
//   zoom         — logical pixels per world unit.
//
// Input:
//   Middle-mouse drag or left-mouse drag in the viewport area → pan.
//   Scroll wheel → zoom centred on the cursor.
// ---------------------------------------------------------------------------

class Viewport2D {
public:
    Viewport2D() = default;

    // Call once after the GL context is ready.
    bool init();

    // Call when the logical window size changes.
    void set_viewport(float w, float h);

    // Manually position the world origin in screen space (e.g., to centre the
    // view after the first set_viewport call).
    void set_pan(float x, float y) { _pan_x = x; _pan_y = y; }

    // Draw the grid into the area below header_h.
    void draw(float header_h);

    // GLFW event forwarding — called by App callbacks.
    void handle_mouse_move  (float mx, float my);
    void handle_mouse_button(float mx, float my, bool pressed, bool released, int btn);
    void handle_scroll      (float mx, float my, float delta_y);

private:
    Grid  _grid;
    float _vp_w  = 800.f;
    float _vp_h  = 600.f;
    float _pan_x = 400.f;   // world origin in screen space
    float _pan_y = 300.f;
    float _zoom  = 50.f;    // logical pixels per world unit

    // Panning drag state
    bool  _panning       = false;
    int   _pan_btn       = -1;   // GLFW button that started the pan
    float _drag_start_mx = 0.f;
    float _drag_start_my = 0.f;
    float _pan_at_drag_x = 0.f;
    float _pan_at_drag_y = 0.f;

    float _mouse_x = 0.f;
    float _mouse_y = 0.f;
};

} // namespace bl_ui
