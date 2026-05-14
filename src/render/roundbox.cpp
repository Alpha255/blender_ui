#include "roundbox.h"
#include <cmath>

namespace bl_ui {

// ---------------------------------------------------------------------------
// GLSL shaders
// Screen coordinate system: y=0 at top, increasing downward.
// The vertex shader flips y to match OpenGL's NDC (y=0 at bottom).
// ---------------------------------------------------------------------------

static const char* VERT_SRC = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;  // unit quad [0,1]^2

uniform vec4  uRect;       // x, y, width, height  (screen px, y=0 top)
uniform vec2  uViewport;   // viewport width, height

out vec2 vUV;

void main() {
    vUV = aPos;
    vec2 pos = uRect.xy + aPos * uRect.zw;
    // Convert screen coords (top-left origin) to OpenGL NDC
    float nx =  (pos.x / uViewport.x) * 2.0 - 1.0;
    float ny = -(pos.y / uViewport.y) * 2.0 + 1.0;
    gl_Position = vec4(nx, ny, 0.0, 1.0);
}
)GLSL";

static const char* FRAG_SRC = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 fragColor;

uniform vec2  uSize;       // rect size in px (for SDF space)
uniform vec4  uRadius4;    // per-corner radii px: top-left, top-right, bottom-left, bottom-right
uniform vec4  uColorBg;    // inner fill (RGBA, premultiplied expected)
uniform vec4  uColorOut;   // outline color
uniform float uOutWidth;   // outline thickness px (0 = no outline)

// Per-corner SDF. Coordinate space: p.y < 0 = top half, p.y > 0 = bottom half
// (screen y=0 is at top, so p.y increases downward in SDF space).
// uRadius4: (top-left, top-right, bottom-left, bottom-right)
float roundedRectSDF(vec2 p, vec2 b, vec4 r) {
    float rx = (p.x > 0.0) ? ((p.y > 0.0) ? r.w : r.y)
                            : ((p.y > 0.0) ? r.z : r.x);
    vec2 d = abs(p) - b + rx;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - rx;
}

void main() {
    vec2 p = (vUV - 0.5) * uSize;
    float d = roundedRectSDF(p, uSize * 0.5, uRadius4);

    float maxR = max(max(uRadius4.x, uRadius4.y), max(uRadius4.z, uRadius4.w));
    float alpha;
    if (maxR < 0.5 && uOutWidth <= 0.0) {
        // Axis-aligned solid rect (lines, fills): hard edge, no AA blur.
        alpha = d < 0.0 ? 1.0 : 0.0;
    } else {
        float aa = fwidth(d);
        alpha = 1.0 - smoothstep(-aa, aa, d);
    }

    vec4 col;
    if (uOutWidth > 0.0 && d > -uOutWidth) {
        col = uColorOut;
    } else {
        col = uColorBg;
    }
    fragColor = vec4(col.rgb, col.a * alpha);
}
)GLSL";

// ---------------------------------------------------------------------------
// Solid-color triangle shader (for submenu arrows and line segments)
// Mirrors Blender's draw_anti_tria_rect / immBegin(GPU_PRIM_TRIS) approach.
// ---------------------------------------------------------------------------

static const char* TRI_VERT = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
uniform vec2 uViewport;
void main() {
    float nx =  (aPos.x / uViewport.x) * 2.0 - 1.0;
    float ny = -(aPos.y / uViewport.y) * 2.0 + 1.0;
    gl_Position = vec4(nx, ny, 0.0, 1.0);
}
)GLSL";

static const char* TRI_FRAG = R"GLSL(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() { fragColor = uColor; }
)GLSL";

// Shared vertex layout descriptors (defined once, reused every draw call).
static const gfx::VertexAttr ATTR_POS2[] = { {0, 2, 0} };
static const gfx::VertexLayout LAYOUT_POS2 = { ATTR_POS2, 1, 2 * sizeof(float) };

// ---------------------------------------------------------------------------
// Roundbox
// ---------------------------------------------------------------------------

bool Roundbox::init(gfx::Backend& gfx) {
    _gfx = &gfx;

    _sh_rb = gfx.create_shader(VERT_SRC, FRAG_SRC);
    if (!_sh_rb) return false;

    // Static unit quad: two triangles covering [0,1]^2.
    float quad[] = {
        0.f, 0.f,   1.f, 0.f,   1.f, 1.f,
        0.f, 0.f,   1.f, 1.f,   0.f, 1.f,
    };
    _quad = gfx.create_buffer(quad, sizeof(quad), false);
    if (!_quad) return false;

    _sh_tri = gfx.create_shader(TRI_VERT, TRI_FRAG);
    if (!_sh_tri) return false;

    // Dynamic buffer: up to 6 vertices for triangles / line segments.
    _tri_buf = gfx.create_buffer(nullptr, 6 * 2 * sizeof(float), true);
    if (!_tri_buf) return false;

    // Dynamic buffer: 12 vertices (4 triangles) for the checkmark shape.
    _check_buf = gfx.create_buffer(nullptr, 12 * 2 * sizeof(float), true);
    return _check_buf != 0;
}

void Roundbox::set_viewport(float width, float height) {
    _vp_w = width;
    _vp_h = height;
}

void Roundbox::draw_roundbox(float x, float y, float w, float h,
                              float r_tl, float r_tr, float r_bl, float r_br,
                              RGBA fill, RGBA outline, float outline_w) {
    _gfx->set_blend_alpha(true);
    _gfx->use_shader(_sh_rb);
    _gfx->uniform_4f("uRect",     x, y, w, h);
    _gfx->uniform_2f("uViewport", _vp_w, _vp_h);
    _gfx->uniform_2f("uSize",     w, h);
    _gfx->uniform_4f("uRadius4",  r_tl, r_tr, r_bl, r_br);
    _gfx->uniform_4f("uColorBg",  fill.rf(),    fill.gf(),    fill.bf(),    fill.af());
    _gfx->uniform_4f("uColorOut", outline.rf(), outline.gf(), outline.bf(), outline.af());
    _gfx->uniform_1f("uOutWidth", outline_w);
    _gfx->draw_triangles(_quad, LAYOUT_POS2, 0, 6);
}

void Roundbox::draw_roundbox(float x, float y, float w, float h,
                              float radius,
                              RGBA fill, RGBA outline, float outline_w) {
    draw_roundbox(x, y, w, h, radius, radius, radius, radius,
                  fill, outline, outline_w);
}

void Roundbox::draw_rect_filled(float x, float y, float w, float h, RGBA color) {
    draw_roundbox(x, y, w, h, 0.f, color);
}

void Roundbox::draw_line_h(float x, float y, float len, RGBA color) {
    draw_roundbox(x, std::round(y), len, 1.f, 0.f, color);
}

void Roundbox::draw_line_v(float x, float y, float h, RGBA color) {
    draw_roundbox(x, y, 1.f, h, 0.f, color);
}

void Roundbox::draw_triangle_right(float x, float y, float w, float h,
                                    RGBA color) {
    float verts[3][2] = {
        {x,     y},
        {x,     y + h},
        {x + w, y + h * 0.5f},
    };
    _gfx->update_buffer(_tri_buf, verts, sizeof(verts));
    _gfx->set_blend_alpha(true);
    _gfx->use_shader(_sh_tri);
    _gfx->uniform_2f("uViewport", _vp_w, _vp_h);
    _gfx->uniform_4f("uColor", color.rf(), color.gf(), color.bf(), color.af());
    _gfx->draw_triangles(_tri_buf, LAYOUT_POS2, 0, 3);
}

void Roundbox::draw_line_segment(float x0, float y0, float x1, float y1,
                                  float width, RGBA color) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;

    float hw = width * 0.5f;
    float px = -dy / len * hw;
    float py =  dx / len * hw;

    float verts[6][2] = {
        {x0 + px, y0 + py}, {x0 - px, y0 - py}, {x1 + px, y1 + py},
        {x0 - px, y0 - py}, {x1 - px, y1 - py}, {x1 + px, y1 + py},
    };
    _gfx->update_buffer(_tri_buf, verts, sizeof(verts));
    _gfx->set_blend_alpha(true);
    _gfx->use_shader(_sh_tri);
    _gfx->uniform_2f("uViewport", _vp_w, _vp_h);
    _gfx->uniform_4f("uColor", color.rf(), color.gf(), color.bf(), color.af());
    _gfx->draw_triangles(_tri_buf, LAYOUT_POS2, 0, 6);
}

// ---------------------------------------------------------------------------
// draw_checkmark — tick mark geometry from Blender's g_shape_preset_checkmark_vert.
// Source ref: source/blender/editors/interface/interface_widgets.cc:370-377
//
// Vertex space: normalized ~[-0.58, 1.06] × [-0.33, 1.03].
// Blender places center at (cx, cy) = rect_center with cy shifted down 5%,
// then scales by 0.4 * min(w, h).  We apply a y-flip when converting to
// screen space (y=0 top, increasing down).
// ---------------------------------------------------------------------------

void Roundbox::draw_checkmark(float x, float y, float w, float h, RGBA color) {
    // Blender's normalized checkmark vertices.
    static const float verts[6][2] = {
        {-0.578579f,  0.253369f},
        {-0.392773f,  0.412794f},
        {-0.004241f, -0.328551f},
        {-0.003001f,  0.034320f},
        { 1.055313f,  0.864744f},
        { 0.866408f,  1.026895f},
    };
    // Face indices (4 triangles).
    static const int faces[4][3] = {
        {3, 2, 4},
        {3, 4, 5},
        {1, 0, 3},
        {0, 2, 3},
    };

    // Center + scale matching shape_preset_trias_from_rect_checkmark().
    float cx = x + 0.5f * w;
    float cy = y + 0.5f * h + 0.05f * h;  // slight downward shift (Blender: y -= 0.05*h)
    float s  = 0.4f * std::min(w, h);

    // Build screen-space vertex positions: flip y axis for screen coordinates.
    float sv[6][2];
    for (int i = 0; i < 6; i++) {
        sv[i][0] = cx + s * verts[i][0];
        sv[i][1] = cy - s * verts[i][1];
    }

    // Unroll 4 faces into 12 flat vertices for the GPU buffer.
    float buf[12][2];
    for (int f = 0; f < 4; f++) {
        for (int v = 0; v < 3; v++) {
            int vi = faces[f][v];
            buf[f * 3 + v][0] = sv[vi][0];
            buf[f * 3 + v][1] = sv[vi][1];
        }
    }

    _gfx->update_buffer(_check_buf, buf, sizeof(buf));
    _gfx->set_blend_alpha(true);
    _gfx->use_shader(_sh_tri);
    _gfx->uniform_2f("uViewport", _vp_w, _vp_h);
    _gfx->uniform_4f("uColor", color.rf(), color.gf(), color.bf(), color.af());
    _gfx->draw_triangles(_check_buf, LAYOUT_POS2, 0, 12);
}

// ---------------------------------------------------------------------------
// draw_softshadow
// Replicates Blender's widget_softshadow(rect, CNR_ALL, radin).
// Blender draws 5 expanding rounded-rect rings with decreasing alpha,
// from inner (closest to box, darkest) to outer (farthest, lightest).
// We render outer → inner so each subsequent draw overwrites the center,
// leaving only the ring border visible as the shadow.
// ---------------------------------------------------------------------------

void Roundbox::draw_softshadow(float x, float y, float w, float h,
                                float radius, float shadow_px) {
    const int steps = 5;
    for (int i = 0; i < steps; ++i) {
        // i=0: outermost ring (most expansion, lightest alpha)
        // i=steps-1: innermost ring (least expansion, darkest alpha)
        float t   = float(steps - i) / float(steps);  // 1.0 → 0.2
        float exp = shadow_px * t;
        // Alpha increases as we get closer to the box edge.
        // Matches Blender's approach: max alpha ≈ 30-40 for dark theme.
        unsigned char a = (unsigned char)(8.f * float(i + 1));  // 8..40
        RGBA c{0, 0, 0, a};
        draw_roundbox(x - exp * 0.5f, y - exp * 0.5f,
                      w + exp, h + exp,
                      radius + exp * 0.5f, c);
    }
}

} // namespace bl_ui
