#include "viewport.h"
#include "../render/theme.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace bl_ui {

static constexpr float PI          = 3.14159265f;
static constexpr float ELEV_MAX    = 85.f * PI / 180.f;
static constexpr float DIST_MIN    = 0.01f;
static constexpr float DIST_MAX    = 100000.f;
static constexpr float DOLLY_STEP  = 0.1f;   // distance factor per scroll notch

bool Viewport3D::init() {
    if (!_grid.init()) {
        std::cerr << "[bl_ui] Viewport3D: Grid init failed\n";
        return false;
    }
    _update_matrices();
    return true;
}

void Viewport3D::set_viewport(float w, float h) {
    _vp_w = w;
    _vp_h = h;
    _grid.set_viewport(w, h);
    _update_matrices();
}

void Viewport3D::_update_matrices() {
    // Eye position in spherical coordinates around target.
    float ce = std::cos(_elevation), se = std::sin(_elevation);
    float ca = std::cos(_azimuth),   sa = std::sin(_azimuth);
    _eye = {
        _target.x + _distance * ce * sa,
        _target.y + _distance * se,
        _target.z + _distance * ce * ca,
    };

    float aspect = _vp_w / std::max(_vp_h, 1.f);
    Mat4 proj = Mat4::perspective(_fov_y, aspect, 0.01f, 50000.f);
    Mat4 view = Mat4::look_at(_eye, _target, Vec3{0.f, 1.f, 0.f});
    _view_proj = proj * view;
    _view_proj.inverse(_inv_view_proj);
}

void Viewport3D::draw(float header_h) {
    _update_matrices();
    _grid.draw(_view_proj, _inv_view_proj, _eye, _distance, header_h);
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void Viewport3D::handle_mouse_move(float mx, float my) {
    float dx = mx - _mouse_x;
    float dy = my - _mouse_y;
    _mouse_x = mx;
    _mouse_y = my;

    if (!_dragging) return;

    if (_drag_btn == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (_drag_mods & GLFW_MOD_SHIFT) {
            // Pan: translate target in the camera's right/up plane.
            // Scale so 1 px of drag moves proportionally to distance and FOV.
            float ce0 = std::cos(_drag_el0), se0 = std::sin(_drag_el0);
            float ca0 = std::cos(_drag_az0), sa0 = std::sin(_drag_az0);
            Vec3 eye0 = {
                _drag_target0.x + _distance * ce0 * sa0,
                _drag_target0.y + _distance * se0,
                _drag_target0.z + _distance * ce0 * ca0,
            };
            Vec3 fwd   = (_drag_target0 - eye0).normalized();
            Vec3 world_up = {0.f, 1.f, 0.f};
            Vec3 right = fwd.cross(world_up).normalized();
            Vec3 up    = right.cross(fwd);   // orthonormal, no renormalize needed

            float scale = _distance * std::tan(_fov_y * 0.5f) * 2.f / _vp_h;

            float total_dx = mx - _drag_start_mx;
            float total_dy = my - _drag_start_my;

            _target = _drag_target0
                    + right * (-total_dx * scale)
                    + up    * ( total_dy * scale);
        } else {
            // Orbit: update azimuth and elevation from total drag distance.
            float total_dx = mx - _drag_start_mx;
            float total_dy = my - _drag_start_my;

            _azimuth   = _drag_az0 - total_dx * (2.f * PI / _vp_w);
            _elevation = std::clamp(_drag_el0 + total_dy * (PI / _vp_h),
                                    -ELEV_MAX, ELEV_MAX);
        }
    }
}

void Viewport3D::handle_mouse_button(float mx, float my,
                                     bool pressed, bool released,
                                     int btn, int mods) {
    if (pressed && !_dragging && btn == GLFW_MOUSE_BUTTON_MIDDLE) {
        _dragging      = true;
        _drag_btn      = btn;
        _drag_mods     = mods;
        _drag_start_mx = mx;
        _drag_start_my = my;
        _drag_az0      = _azimuth;
        _drag_el0      = _elevation;
        _drag_target0  = _target;
    }

    if (released && _drag_btn == btn) {
        _dragging  = false;
        _drag_btn  = -1;
        _drag_mods = 0;
    }
}

void Viewport3D::handle_scroll(float /*mx*/, float /*my*/, float delta_y) {
    // Dolly: scale distance by a fixed factor per notch.
    float factor = (delta_y > 0.f) ? (1.f - DOLLY_STEP) : (1.f + DOLLY_STEP);
    _distance = std::clamp(_distance * factor, DIST_MIN, DIST_MAX);
}

} // namespace bl_ui
