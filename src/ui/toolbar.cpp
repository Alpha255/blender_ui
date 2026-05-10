#include "toolbar.h"
#include "../render/theme.h"
#include <bl_ui/icons.h>
#include <algorithm>
#include <cmath>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Visual constants
// ---------------------------------------------------------------------------

static constexpr RGBA PANEL_BG      { 26,  26,  26, 215};
static constexpr RGBA PANEL_BORDER  { 48,  48,  48, 255};
static constexpr RGBA BTN_NORMAL    { 52,  52,  52, 255};
static constexpr RGBA BTN_HOVER     { 78,  78,  78, 255};
static constexpr RGBA BTN_ACTIVE    { 48,  88, 148, 255};  // Blender active-tool blue
static constexpr RGBA BTN_OUTLINE   { 18,  18,  18, 255};
static constexpr RGBA ICON_NORMAL   {185, 185, 185, 255};
static constexpr RGBA ICON_ACTIVE   {255, 255, 255, 255};
static constexpr RGBA SEP_LINE      { 55,  55,  55, 255};

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void Toolbar::set_viewport(float vp_w, float vp_h) {
    _vp_w = vp_w;
    _vp_h = vp_h;
}

void Toolbar::_layout(float header_h) {
    using namespace Theme;
    _header_h = header_h;

    const float margin   = std::roundf(4.f  * UI_SCALE);
    const float btn_size = std::roundf(36.f * UI_SCALE);
    const float btn_gap  = std::roundf(2.f  * UI_SCALE);
    const float sep_gap  = std::roundf(6.f  * UI_SCALE);  // gap between groups

    _pw = margin + btn_size + margin;
    _px = margin;
    _py = header_h + margin;

    float by = _py + margin;
    for (int i = 0; i < TOOL_COUNT; ++i) {
        // Extra gap between Rotate and Scale (separates "grab" from "transform" group)
        if (i == 2) by += sep_gap;
        _btns[i] = { _px + margin, by, btn_size, btn_size };
        by += btn_size + btn_gap;
    }
    _ph = (by - btn_gap) - _py + margin;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void Toolbar::draw(float header_h, Roundbox* rb, Font* font) {
    using namespace Theme;
    _layout(header_h);

    // Panel background with rounded corners
    rb->draw_roundbox(_px, _py, _pw, _ph,
                      MENU_RADIUS, PANEL_BG, PANEL_BORDER, 1.f);

    // Thin separator line between group 1 (Move/Rotate) and group 2 (Scale/Transform)
    {
        float sep_y = (_btns[1].y + _btns[1].h + _btns[2].y) * 0.5f;
        float sx    = _px + std::roundf(4.f * UI_SCALE);
        float sw    = _pw - std::roundf(8.f * UI_SCALE);
        rb->draw_line_h(sx, sep_y, sw, SEP_LINE);
    }

    // Atlas icon IDs, one per tool
    static const int ICON_IDS[TOOL_COUNT] = {
        ICON_TRANSFORM_MOVE,
        ICON_TRANSFORM_ROTATE,
        ICON_TRANSFORM_SCALE,
        ICON_TRANSFORM_ALL,
    };

    // Procedural icon fallbacks (used when atlas is unavailable)
    using IconFn = void(*)(Roundbox*, float, float, float, RGBA);
    static const IconFn PROC_FNS[TOOL_COUNT] = {
        _icon_move, _icon_rotate, _icon_scale, _icon_transform
    };

    static const char* LABELS[TOOL_COUNT] = {
        "Move", "Rotate", "Scale", "Transform"
    };

    for (int i = 0; i < TOOL_COUNT; ++i) {
        const BtnRect& b = _btns[i];
        bool is_active = (_active == static_cast<Tool>(i));
        bool is_hov    = (_hov_btn == i);

        RGBA fill = is_active ? BTN_ACTIVE : (is_hov ? BTN_HOVER : BTN_NORMAL);
        rb->draw_roundbox(b.x, b.y, b.w, b.h,
                          MENU_RADIUS, fill, BTN_OUTLINE, 1.f);

        RGBA icol = (is_active || is_hov) ? ICON_ACTIVE : ICON_NORMAL;

        if (_icons && _icons->ready()) {
            // Atlas icon: centre it inside the button
            float isz = _icons->icon_size();
            float ix  = std::roundf(b.x + (b.w - isz) * 0.5f);
            float iy  = std::roundf(b.y + (b.h - isz) * 0.5f);
            _icons->draw_icon(ICON_IDS[i], ix, iy, icol);
        } else {
            // Procedural fallback
            float cx = b.x + b.w * 0.5f;
            float cy = b.y + b.h * 0.5f;
            float sz = b.w * 0.52f;
            PROC_FNS[i](rb, cx, cy, sz, icol);
        }

        // Tooltip label to the right of the panel when hovered
        if (is_hov && font) {
            float tw  = font->measure_text(LABELS[i]);
            float th  = font->line_height();
            float lx  = _px + _pw + std::roundf(6.f * UI_SCALE);
            float ly  = b.y + (b.h - th) * 0.5f;
            float pad = std::roundf(4.f * UI_SCALE);
            rb->draw_roundbox(lx - pad, ly - pad * 0.5f,
                              tw + pad * 2.f, th + pad,
                              MENU_RADIUS,
                              RGBA{20, 20, 20, 230},
                              PANEL_BORDER, 1.f);
            font->draw_text(LABELS[i], lx, ly, ICON_ACTIVE);
        }
    }
    (void)font;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool Toolbar::handle_mouse(float mx, float my, bool pressed, bool /*released*/) {
    bool over_panel = (mx >= _px && mx < _px + _pw &&
                       my >= _py && my < _py + _ph);

    _hov_btn = -1;
    if (over_panel) {
        for (int i = 0; i < TOOL_COUNT; ++i) {
            const BtnRect& b = _btns[i];
            if (mx >= b.x && mx < b.x + b.w &&
                my >= b.y && my < b.y + b.h)
            {
                _hov_btn = i;
                break;
            }
        }
    }

    if (pressed && _hov_btn >= 0)
        _active = static_cast<Tool>(_hov_btn);

    return over_panel;
}

// ---------------------------------------------------------------------------
// Procedural icon helpers
// ---------------------------------------------------------------------------

// Draw one arrow arm: shaft from center out to (tip - arrowhead), then
// V-shaped arrowhead at tip.
// dx, dy  — unit direction vector toward the arrow tip
// sr      — shaft start distance from center (leaves center clear for dot)
// tip     — distance from center to arrow tip
// ah      — arrowhead "height" (how far back the barbs go from tip)
// aw      — arrowhead half-width
void Toolbar::_arrow(Roundbox* rb,
                     float cx, float cy, float dx, float dy,
                     float sr, float tip, float ah, float aw,
                     float lw, RGBA col)
{
    // Shaft
    float x0 = cx + dx * sr;
    float y0 = cy + dy * sr;
    float x1 = cx + dx * (tip - ah);
    float y1 = cy + dy * (tip - ah);
    rb->draw_line_segment(x0, y0, x1, y1, lw, col);

    // Arrowhead: two barbs from tip back to (x1 ± perp*aw)
    float tx = cx + dx * tip;
    float ty = cy + dy * tip;
    float px = -dy, py = dx;   // perpendicular (rotate 90°)
    rb->draw_line_segment(tx, ty, x1 + px * aw, y1 + py * aw, lw, col);
    rb->draw_line_segment(tx, ty, x1 - px * aw, y1 - py * aw, lw, col);
}

// Move — four cardinal arrows + center dot
void Toolbar::_icon_move(Roundbox* rb, float cx, float cy, float sz, RGBA col) {
    const float arm = sz * 0.48f;
    const float sr  = sz * 0.08f;
    const float ah  = sz * 0.20f;
    const float aw  = sz * 0.16f;
    const float lw  = std::max(1.2f, sz * 0.11f);

    // y-axis points DOWN in screen coords
    const float D[4][2] = {{0.f,-1.f},{1.f,0.f},{0.f,1.f},{-1.f,0.f}};
    for (auto& d : D)
        _arrow(rb, cx, cy, d[0], d[1], sr, arm, ah, aw, lw, col);

    float dr = lw * 0.85f;
    rb->draw_roundbox(cx - dr, cy - dr, dr * 2.f, dr * 2.f, dr,
                      col, {0,0,0,0}, 0.f);
}

// Rotate — open arc (~300 °) with a clockwise arrowhead at the gap
void Toolbar::_icon_rotate(Roundbox* rb, float cx, float cy, float sz, RGBA col) {
    const float r  = sz * 0.40f;
    const float lw = std::max(1.2f, sz * 0.11f);
    const int   N  = 16;

    // Arc from start_ang to end_ang (clockwise gap at ~top-right)
    const float start = 0.45f;   // ~26°
    const float end   = 5.83f;   // ~334° — leaves a ~40° gap

    float px = cx + r * std::cosf(start);
    float py = cy + r * std::sinf(start);
    for (int i = 1; i <= N; ++i) {
        float t  = start + (end - start) * float(i) / float(N);
        float nx = cx + r * std::cosf(t);
        float ny = cy + r * std::sinf(t);
        rb->draw_line_segment(px, py, nx, ny, lw, col);
        px = nx; py = ny;
    }

    // Arrowhead at the START of the arc (clockwise tangent direction)
    //   clockwise tangent at angle a: (+sin(a), -cos(a))
    float tdx =  std::sinf(start);
    float tdy = -std::cosf(start);
    float ah  = sz * 0.20f;
    float aw  = sz * 0.15f;
    float tip_x = cx + r * std::cosf(start);
    float tip_y = cy + r * std::sinf(start);
    // Back-point along NEGATIVE tangent (opposite direction from clockwise)
    float bx = tip_x - tdx * ah;
    float by_ = tip_y - tdy * ah;
    float perpx = tdy, perpy = -tdx;
    rb->draw_line_segment(tip_x, tip_y, bx + perpx * aw, by_ + perpy * aw, lw, col);
    rb->draw_line_segment(tip_x, tip_y, bx - perpx * aw, by_ - perpy * aw, lw, col);
}

// Scale — four diagonal arrows pointing outward + center dot
void Toolbar::_icon_scale(Roundbox* rb, float cx, float cy, float sz, RGBA col) {
    static const float INV_SQRT2 = 0.70711f;
    const float arm = sz * 0.46f;
    const float sr  = sz * 0.12f;
    const float ah  = sz * 0.19f;
    const float aw  = sz * 0.15f;
    const float lw  = std::max(1.2f, sz * 0.11f);

    const float D[4][2] = {
        {-INV_SQRT2, -INV_SQRT2}, { INV_SQRT2, -INV_SQRT2},
        {-INV_SQRT2,  INV_SQRT2}, { INV_SQRT2,  INV_SQRT2},
    };
    for (auto& d : D)
        _arrow(rb, cx, cy, d[0], d[1], sr, arm, ah, aw, lw, col);

    float dr = lw * 0.85f;
    rb->draw_roundbox(cx - dr, cy - dr, dr * 2.f, dr * 2.f, dr,
                      col, {0,0,0,0}, 0.f);
}

// Transform — full ring (represents rotation) + inner crosshair (move/scale)
void Toolbar::_icon_transform(Roundbox* rb, float cx, float cy, float sz, RGBA col) {
    const float ring_r = sz * 0.44f;
    const float lw     = std::max(1.2f, sz * 0.10f);

    // Outer ring
    {
        const int N = 20;
        float px = cx + ring_r, py = cy;
        for (int i = 1; i <= N; ++i) {
            float t  = float(i) * 6.28318f / float(N);
            float nx = cx + ring_r * std::cosf(t);
            float ny = cy + ring_r * std::sinf(t);
            rb->draw_line_segment(px, py, nx, ny, lw * 0.80f, col);
            px = nx; py = ny;
        }
        // Arrowhead on ring at top (angle = -π/2), clockwise direction
        float ang  = -1.5708f;
        float tdx  =  std::sinf(ang);   // clockwise tangent
        float tdy  = -std::cosf(ang);
        float ah   = sz * 0.17f, aw = sz * 0.13f;
        float tip_x = cx + ring_r * std::cosf(ang);
        float tip_y = cy + ring_r * std::sinf(ang);
        float bx    = tip_x - tdx * ah;
        float by_   = tip_y - tdy * ah;
        float perpx = tdy, perpy = -tdx;
        rb->draw_line_segment(tip_x, tip_y, bx + perpx*aw, by_ + perpy*aw, lw*0.80f, col);
        rb->draw_line_segment(tip_x, tip_y, bx - perpx*aw, by_ - perpy*aw, lw*0.80f, col);
    }

    // Inner crosshair (smaller move icon)
    {
        const float inner_arm = ring_r * 0.56f;
        const float sr        = ring_r * 0.14f;
        const float ah        = inner_arm * 0.34f;
        const float aw        = inner_arm * 0.28f;
        const float D[4][2]   = {{0.f,-1.f},{1.f,0.f},{0.f,1.f},{-1.f,0.f}};
        for (auto& d : D)
            _arrow(rb, cx, cy, d[0], d[1], sr, inner_arm, ah, aw, lw, col);

        float dr = lw * 0.85f;
        rb->draw_roundbox(cx - dr, cy - dr, dr * 2.f, dr * 2.f, dr,
                          col, {0,0,0,0}, 0.f);
    }
}

} // namespace bl_ui
