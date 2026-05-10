#include "viewport.h"
#include "../render/theme.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace bl_ui {

static constexpr float ZOOM_MIN =    0.5f;   // 0.5 px per world unit (very zoomed out)
static constexpr float ZOOM_MAX = 5000.f;    // 5000 px per world unit (very zoomed in)
static constexpr float ZOOM_STEP =    1.1f;  // factor per scroll notch

bool Viewport2D::init() {
    if (!_grid.init()) {
        std::cerr << "[bl_ui] Viewport2D: Grid init failed\n";
        return false;
    }
    return true;
}

void Viewport2D::set_viewport(float w, float h) {
    _vp_w = w;
    _vp_h = h;
    _grid.set_viewport(w, h);
}

void Viewport2D::draw(float header_h) {
    _grid.draw(_pan_x, _pan_y, _zoom, header_h);
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void Viewport2D::handle_mouse_move(float mx, float my) {
    _mouse_x = mx;
    _mouse_y = my;

    if (_panning) {
        // Translate the world origin by the cursor delta since drag start.
        _pan_x = _pan_at_drag_x + (mx - _drag_start_mx);
        _pan_y = _pan_at_drag_y + (my - _drag_start_my);
    }
}

void Viewport2D::handle_mouse_button(float mx, float my,
                                     bool pressed, bool released, int btn) {
    if (pressed && !_panning) {
        _panning       = true;
        _pan_btn       = btn;
        _drag_start_mx = mx;
        _drag_start_my = my;
        _pan_at_drag_x = _pan_x;
        _pan_at_drag_y = _pan_y;
    }

    if (released && _pan_btn == btn) {
        _panning = false;
        _pan_btn = -1;
    }
}

void Viewport2D::handle_scroll(float mx, float my, float delta_y) {
    // Zoom centred on cursor: keep the world point under the cursor fixed.
    float factor   = (delta_y > 0.f) ? ZOOM_STEP : (1.f / ZOOM_STEP);
    float zoom_new = std::clamp(_zoom * factor, ZOOM_MIN, ZOOM_MAX);
    float ratio    = zoom_new / _zoom;

    // pan_new = cursor - (cursor - pan_old) * ratio
    _pan_x = mx - (mx - _pan_x) * ratio;
    _pan_y = my - (my - _pan_y) * ratio;
    _zoom  = zoom_new;
}

} // namespace bl_ui
