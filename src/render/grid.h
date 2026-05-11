#pragma once
#include "theme.h"
#include "mat4.h"
#include "gfx/backend.h"

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
    ~Grid() = default;

    bool init(gfx::Backend& gfx);
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
    static float _spacing(float distance);

    gfx::Backend*      _gfx = nullptr;
    gfx::ShaderHandle  _sh  = 0;
    gfx::BufferHandle  _vbo = 0;   // full-screen quad, updated each frame
    float _vp_w = 800.f, _vp_h = 600.f;
};

} // namespace bl_ui
