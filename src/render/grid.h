#pragma once
#include <GL/glew.h>
#include "theme.h"
#include "mat4.h"

namespace bl_ui {

// ---------------------------------------------------------------------------
// Grid — perspective ground-plane (y=0) grid rendered with a full-screen quad.
//
// Algorithm: for each fragment, unproject the NDC position via the inverse
// view-projection matrix to get a world-space ray, intersect with y=0, then
// evaluate an analytical grid at the hit point using fwidth()-based AA.
// fwidth() naturally accounts for perspective foreshortening, so near lines
// are sharp and far lines blend smoothly into the background.
//
// Coordinate system: y-up, right-handed OpenGL.
//   Ground plane: y = 0
//   X axis (red):   z = 0 line on the ground plane
//   Z axis (green): x = 0 line on the ground plane
// ---------------------------------------------------------------------------

class Grid {
public:
    Grid()  = default;
    ~Grid();

    bool init();
    void set_viewport(float w, float h);

    // Draw the ground-plane grid.
    //   view_proj     — proj * view (column-major, for writing gl_FragDepth)
    //   inv_view_proj — inverse of view_proj (for ray unprojection)
    //   eye           — camera world position (for distance fade)
    //   distance      — camera-to-target distance (controls grid spacing)
    //   header_h      — logical pixels of the menu bar (suppressed above)
    void draw(const Mat4& view_proj, const Mat4& inv_view_proj,
              const Vec3& eye, float distance, float header_h);

private:
    // Adaptive spacing: minor lines ~10% of distance apart, snapped to {1,2,5}×10^n.
    static float _spacing(float distance);

    GLuint _prog = 0, _vao = 0, _vbo = 0;
    float  _vp_w = 800.f, _vp_h = 600.f;

    GLint _u_view_proj   = -1;
    GLint _u_inv_vp      = -1;
    GLint _u_eye         = -1;
    GLint _u_viewport    = -1;
    GLint _u_header_h    = -1;
    GLint _u_spacing_min = -1;
    GLint _u_spacing_maj = -1;
    GLint _u_fade_near   = -1;
    GLint _u_fade_far    = -1;
    GLint _u_bg          = -1;
    GLint _u_grid        = -1;
    GLint _u_grid_major  = -1;
    GLint _u_axis_x      = -1;
    GLint _u_axis_z      = -1;
};

} // namespace bl_ui
