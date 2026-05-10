#pragma once
#include "roundbox.h"
#include "font.h"
#include "mat4.h"

namespace bl_ui {

// ---------------------------------------------------------------------------
// NavGizmo — viewport navigation gizmo drawn in the top-right corner.
//
// Matches Blender's VIEW3D_GGT_navigate widget:
//   source/blender/editors/space_view3d/view3d_gizmo_navigate_type.cc
//
// Visual: six axis handles (+X/-X, +Y/-Y, +Z/-Z) projected from the current
// view rotation matrix, depth-sorted and drawn back-to-front (painter's).
// Axis colors: X=#ff3352  Y=#8bdc00  Z=#2890ff  (Blender default theme).
// ---------------------------------------------------------------------------

class NavGizmo {
public:
    // view      — view matrix from look_at (4×4 column-major).  Only the
    //             upper-left 3×3 rotation block is used.
    // vp_w/h    — logical viewport dimensions (screen pixels, y=0 at top).
    // header_h  — height of the menu bar (gizmo positioned below it).
    void draw(const Mat4& view,
              float vp_w, float vp_h, float header_h,
              Roundbox* rb, Font* font);
};

} // namespace bl_ui
