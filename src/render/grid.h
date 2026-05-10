#pragma once
#include <GL/glew.h>
#include "theme.h"

namespace bl_ui {

// ---------------------------------------------------------------------------
// Grid — infinite analytical 2D grid rendered with a single full-viewport quad.
//
// The fragment shader uses fwidth()-based anti-aliasing so grid lines stay
// exactly 1 screen pixel wide regardless of zoom level.  Minor and major
// subdivision lines are drawn analytically; no geometry is generated per line.
//
// Coordinate conventions (same as the rest of the UI):
//   Screen space  — y=0 at top, increasing downward, logical pixels.
//   World space   — right-handed 2D: x right, y downward (matches screen).
//   pan_x/pan_y   — screen position of the world origin (0,0), logical px.
//   zoom          — logical pixels per world unit.
// ---------------------------------------------------------------------------

class Grid {
public:
    Grid()  = default;
    ~Grid();

    bool init();
    void set_viewport(float w, float h);

    // Draw the full-viewport grid.
    //   pan_x, pan_y  — screen-space position of world origin (logical px)
    //   zoom          — logical pixels per world unit
    //   header_h      — height of the menu bar; grid is suppressed above it
    void draw(float pan_x, float pan_y, float zoom, float header_h);

private:
    // Choose the minor-grid world-unit spacing that puts ~80 px between lines.
    static float _spacing(float zoom);

    GLuint _prog = 0, _vao = 0, _vbo = 0;
    float  _vp_w = 800.f, _vp_h = 600.f;

    GLint _u_viewport     = -1;
    GLint _u_pan          = -1;
    GLint _u_zoom         = -1;
    GLint _u_header_h     = -1;
    GLint _u_spacing      = -1;
    GLint _u_major_factor = -1;
    GLint _u_bg           = -1;
    GLint _u_grid         = -1;
    GLint _u_grid_major   = -1;
    GLint _u_axis_x       = -1;
    GLint _u_axis_y       = -1;
};

} // namespace bl_ui
