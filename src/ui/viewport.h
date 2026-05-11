#pragma once
#include "../render/grid.h"
#include "../render/mat4.h"
#include "../render/nav_gizmo.h"
#include "../render/roundbox.h"
#include "../render/font.h"
#include "../render/gfx/backend.h"

namespace bl_ui {

// ---------------------------------------------------------------------------
// Viewport3D — perspective 3D viewport with orbit camera and ground-plane grid.
//
// Camera model:
//   target    — world point the camera orbits around
//   azimuth   — horizontal rotation (radians, 0 = front: eye on +Z axis)
//   elevation — vertical tilt (radians, clamped to ±85°; positive = above)
//   distance  — camera-to-target distance
//   fov_y     — vertical field of view (radians)
//
// Controls:
//   MMB drag         → orbit (azimuth + elevation)
//   Shift+MMB drag   → pan (translate target in camera right/up plane)
//   Scroll wheel     → dolly (scale distance)
// ---------------------------------------------------------------------------

class Viewport3D {
public:
    Viewport3D() = default;

    bool init(gfx::Backend& gfx);
    void set_viewport(float w, float h);
    void set_dependencies(Roundbox* rb, Font* font);
    void draw(float header_h);

    // GLFW event forwarding — called by App callbacks.
    void handle_mouse_move  (float mx, float my);
    void handle_mouse_button(float mx, float my, bool pressed, bool released,
                             int btn, int mods);
    void handle_scroll      (float mx, float my, float delta_y);

private:
    void _update_matrices();

    Grid      _grid;
    NavGizmo  _gizmo;
    Roundbox* _rb   = nullptr;
    Font*     _font = nullptr;
    float _vp_w = 800.f, _vp_h = 600.f;

    // Orbit camera
    Vec3  _target    = {0.f, 0.f, 0.f};
    float _azimuth   = 0.7854f;   // 45° — isometric-ish start
    float _elevation = 0.6154f;   // ~35.26° — classic "cube corner" angle
    float _distance  = 10.f;
    float _fov_y     = 1.0472f;   // 60°

    // Cached per-frame matrices
    Mat4  _view;
    Mat4  _view_proj;
    Mat4  _inv_view_proj;
    Vec3  _eye;

    // Drag state
    bool  _dragging      = false;
    int   _drag_btn      = -1;
    int   _drag_mods     = 0;
    float _drag_start_mx = 0.f;
    float _drag_start_my = 0.f;
    float _drag_az0      = 0.f;   // azimuth  at drag start
    float _drag_el0      = 0.f;   // elevation at drag start
    Vec3  _drag_target0  = {};    // target    at drag start

    float _mouse_x = 0.f;
    float _mouse_y = 0.f;
};

} // namespace bl_ui
