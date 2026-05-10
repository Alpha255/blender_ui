#include "nav_gizmo.h"
#include "theme.h"
#include <algorithm>
#include <cmath>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Axis colors — Blender default theme (userdef_default_theme.c)
// X: #ff3352   Y: #8bdc00   Z: #2890ff
// ---------------------------------------------------------------------------

static constexpr RGBA X_COL {255,  51,  82, 255};
static constexpr RGBA Y_COL {139, 220,   0, 255};
static constexpr RGBA Z_COL { 40, 144, 255, 255};

static RGBA blend_rgba(RGBA a, RGBA b, float t) {
    // t=0 → a,  t=1 → b
    auto lerp = [](uint8_t x, uint8_t y, float f) -> uint8_t {
        return static_cast<uint8_t>(x + (y - x) * f);
    };
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t),
            lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}

// ---------------------------------------------------------------------------
// NavGizmo::draw
// ---------------------------------------------------------------------------

void NavGizmo::draw(const Mat4& view,
                    float vp_w, float vp_h, float header_h,
                    Roundbox* rb, Font* font)
{
    using namespace Theme;

    // ---- Sizing (Blender default: 80px diameter → radius 40px) ---------------
    const float R      = std::roundf(52.f * UI_SCALE);   // gizmo radius
    const float hr_pos = std::roundf(9.5f * UI_SCALE);   // positive handle radius
    const float hr_neg = std::roundf(7.0f * UI_SCALE);   // negative handle radius (smaller)
    const float lw     = std::roundf(2.0f * UI_SCALE);   // axis line width
    const float MARGIN = std::roundf(14.f * UI_SCALE);   // gap from viewport edge

    // Gizmo center — top-right corner, below header
    const float cx = vp_w - MARGIN - R;
    const float cy = header_h + MARGIN + R;

    // ---- Project world axes via view rotation --------------------------------
    // View matrix is column-major: m[col*4 + row].
    // Transforming world axis i through the view rotation:
    //   view_x = m[col*4 + 0]   (screen right)
    //   view_y = m[col*4 + 1]   (screen down in view space → flip to screen)
    //   view_z = m[col*4 + 2]   (depth: positive = in front of camera plane)
    // Screen projection: sx = view_x,  sy = -view_y  (flip Y for screen-down)

    struct AxisDef { int col; RGBA color; const char* lbl; };
    static const AxisDef AXES[3] = {
        {0, X_COL, "X"},
        {1, Y_COL, "Y"},
        {2, Z_COL, "Z"},
    };

    // Build 6 handles (positive + negative for each axis)
    struct Handle {
        float  sx, sy;     // unit screen direction from center
        float  depth;
        float  hr;         // handle radius
        RGBA   fill;
        RGBA   line_col;
        const char* label; // nullptr for negative (no label)
        bool   positive;
    };

    Handle handles[6];
    const RGBA BG_DARK{0x1D, 0x1D, 0x1D, 255};  // near VIEWPORT_BG

    for (int i = 0; i < 3; i++) {
        const AxisDef& a = AXES[i];
        int   col = a.col;
        float vx  =  view.m[col * 4 + 0];
        float vy  = -view.m[col * 4 + 1];  // flip Y for screen-down coords
        float vz  =  view.m[col * 4 + 2];

        // Positive end --------------------------------------------------------
        RGBA pos_fill;
        if (vz >= -0.1f) {
            // In front or edge-on: full axis color
            pos_fill = a.color;
        } else {
            // Behind camera plane: fade toward dark background
            float t  = std::min(1.f, (-vz - 0.1f) * 0.35f);
            pos_fill = blend_rgba(a.color, BG_DARK, t);
        }
        RGBA pos_line = pos_fill;
        pos_line.a    = static_cast<uint8_t>(pos_line.a * 0.85f);

        handles[i * 2] = {vx, vy, vz, hr_pos, pos_fill, pos_line,
                          a.lbl, true};

        // Negative end (opposite direction) -----------------------------------
        // "Behind" the center relative to the positive axis.
        // Blend axis color 50% toward white → lighter ghost appearance.
        float neg_vz = -vz;
        RGBA  neg_base = blend_rgba(a.color, RGBA{220, 220, 220, 255}, 0.55f);
        RGBA  neg_fill = neg_base;
        if (neg_vz < 0.f) {
            // This end is also behind: more transparent
            float t    = std::min(1.f, -neg_vz * 0.4f);
            neg_fill.a = static_cast<uint8_t>(neg_fill.a * (1.f - t * 0.5f));
        }
        RGBA neg_line  = neg_fill;
        neg_line.a     = static_cast<uint8_t>(neg_line.a * 0.7f);

        handles[i * 2 + 1] = {-vx, -vy, neg_vz, hr_neg, neg_fill, neg_line,
                               nullptr, false};
    }

    // Sort back-to-front (ascending depth = farthest drawn first)
    std::sort(handles, handles + 6, [](const Handle& a, const Handle& b) {
        return a.depth < b.depth;
    });

    // ---- Phase 1: Axis lines (center → endpoint), back-to-front ------------
    for (const Handle& h : handles) {
        float ex = cx + h.sx * R;
        float ey = cy + h.sy * R;
        rb->draw_line_segment(cx, cy, ex, ey, lw, h.line_col);
    }

    // ---- Phase 2: Circular handles, back-to-front ---------------------------
    for (const Handle& h : handles) {
        float ex = cx + h.sx * R;
        float ey = cy + h.sy * R;
        float r  = h.hr;

        if (h.positive) {
            // Solid filled circle with dark outline (matches Blender's handle)
            rb->draw_roundbox(ex - r, ey - r, r * 2.f, r * 2.f, r,
                              h.fill, RGBA{0x10, 0x10, 0x10, 200}, 1.2f);
        } else {
            // Lighter outlined circle for negative/ghost handle
            RGBA ghost_fill = h.fill;
            ghost_fill.a    = static_cast<uint8_t>(ghost_fill.a * 0.6f);
            rb->draw_roundbox(ex - r, ey - r, r * 2.f, r * 2.f, r,
                              ghost_fill, h.fill, 1.f);
        }
    }

    // ---- Phase 3: Labels ("X" / "Y" / "Z") on positive handles -------------
    // Drawn last so text is always on top of the circles.
    for (const Handle& h : handles) {
        if (!h.positive || !h.label) continue;

        float ex = cx + h.sx * R;
        float ey = cy + h.sy * R;
        float tw = font->measure_text(h.label);
        float th = font->line_height();

        // White text for all positive handles (high contrast on axis colors)
        font->draw_text(h.label,
                        std::roundf(ex - tw * 0.5f),
                        std::roundf(ey - th * 0.5f),
                        RGBA{255, 255, 255, 255});
    }
}

} // namespace bl_ui
