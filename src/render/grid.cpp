#include "grid.h"
#include <cmath>
#include <algorithm>

namespace bl_ui {

// ---------------------------------------------------------------------------
// GLSL shaders
// ---------------------------------------------------------------------------

static const char* GRID_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;   // logical screen pixels, y=0 at top
uniform vec2 uViewport;              // logical (w, h)
out vec2 vPos;

void main() {
    vPos = aPos;
    // Map logical → NDC: x:[0,w]→[-1,1], y:[0,h]→[1,-1] (flip y)
    gl_Position = vec4(
        aPos.x / uViewport.x * 2.0 - 1.0,
        1.0 - aPos.y / uViewport.y * 2.0,
        0.0, 1.0
    );
}
)GLSL";

static const char* GRID_FRAG = R"GLSL(
#version 330 core
in  vec2 vPos;          // logical screen px, y=0 at top
out vec4 fragColor;

uniform mat4  uViewProj;
uniform mat4  uInvViewProj;
uniform vec4  uEye;   // w unused
uniform vec2  uViewport;    // logical (w, h)
uniform float uHeaderH;     // logical px: suppress grid above this y
uniform float uSpacingMin;  // minor grid spacing, world units
uniform float uSpacingMaj;  // major grid spacing, world units
uniform float uFadeNear;    // distance fade start, world units
uniform float uFadeFar;     // distance fade end, world units

uniform vec4 uBgColor;
uniform vec4 uGridColor;
uniform vec4 uGridMajorColor;
uniform vec4 uAxisXColor;   // X axis: z=0 line
uniform vec4 uAxisZColor;   // Z axis: x=0 line

// Analytical AA grid line coverage.
float line_alpha(float world_coord, float spacing) {
    float c   = world_coord / spacing;
    float dfc = max(fwidth(c), 1e-5);
    float d   = abs(fract(c + 0.5) - 0.5);
    return 1.0 - smoothstep(dfc * 0.5, dfc * 1.5, d);
}

void main() {
    gl_FragDepth = 1.0;

    if (vPos.y < uHeaderH) {
        fragColor = uBgColor;
        return;
    }

    vec2 ndc = vec2(
        vPos.x / uViewport.x * 2.0 - 1.0,
        1.0 - vPos.y / uViewport.y * 2.0
    );

    vec4 nh = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 fh = uInvViewProj * vec4(ndc,  1.0, 1.0);
    vec3 near_w = nh.xyz / nh.w;
    vec3 far_w  = fh.xyz / fh.w;
    vec3 ray    = far_w - near_w;

    if (abs(ray.y) < 1e-6) { fragColor = uBgColor; return; }
    float t = -near_w.y / ray.y;
    if (t < 0.0) { fragColor = uBgColor; return; }

    vec3 hit = near_w + t * ray;

    vec4 clip = uViewProj * vec4(hit, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

    float minor_a = max(line_alpha(hit.x, uSpacingMin),
                        line_alpha(hit.z, uSpacingMin));
    float major_a = max(line_alpha(hit.x, uSpacingMaj),
                        line_alpha(hit.z, uSpacingMaj));

    float axX_a = 1.0 - smoothstep(0.0, fwidth(hit.z) * 2.0, abs(hit.z));
    float axZ_a = 1.0 - smoothstep(0.0, fwidth(hit.x) * 2.0, abs(hit.x));

    float dist = length(hit - uEye.xyz);
    float fade = 1.0 - smoothstep(uFadeNear, uFadeFar, dist);

    vec4 col = uBgColor;
    col = mix(col, uGridColor,      minor_a * 0.7 * fade);
    col = mix(col, uGridMajorColor, major_a       * fade);
    col = mix(col, uAxisXColor,     axX_a         * fade);
    col = mix(col, uAxisZColor,     axZ_a         * fade);

    fragColor = col;
}
)GLSL";

static const gfx::VertexAttr ATTR_POS2[] = { {0, 2, 0} };
static const gfx::VertexLayout LAYOUT_POS2 = { ATTR_POS2, 1, 2 * sizeof(float) };

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

bool Grid::init(gfx::Backend& gfx) {
    _gfx = &gfx;
    _sh  = gfx.create_shader(GRID_VERT, GRID_FRAG);
    if (!_sh) return false;

    // Dynamic buffer for the full-screen quad (updated each frame).
    _vbo = gfx.create_buffer(nullptr, 6 * 2 * sizeof(float), true);
    return _vbo != 0;
}

void Grid::set_viewport(float w, float h) {
    _vp_w = w;
    _vp_h = h;
}

float Grid::_spacing(float distance) {
    float target = distance * 0.1f;
    if (target <= 0.f) return 1.f;
    float power = std::pow(10.f, std::floor(std::log10(target)));
    float norm  = target / power;
    if      (norm > 5.f) return power * 10.f;
    else if (norm > 2.f) return power * 5.f;
    else if (norm > 1.f) return power * 2.f;
    return power;
}

void Grid::draw(const Mat4& view_proj, const Mat4& inv_view_proj,
                const Vec3& eye, float distance, float header_h) {
    float w = _vp_w, h = _vp_h;
    float quad[12] = {
        0.f, 0.f,   w,  0.f,   w,  h,
        0.f, 0.f,   w,  h,    0.f, h,
    };
    _gfx->update_buffer(_vbo, quad, sizeof(quad));

    float sp_min   = _spacing(distance);
    float sp_maj   = sp_min * 10.f;
    float fade_near = distance * 5.f;
    float fade_far  = distance * 20.f;

    using namespace Theme;

    // Grid renders depth values unconditionally (GL_ALWAYS) so subsequent 3D
    // geometry composites correctly against the ground plane depth.
    _gfx->set_blend_alpha(false);
    _gfx->set_depth(true, true, /*always_pass=*/true);

    _gfx->use_shader(_sh);
    _gfx->uniform_m4("uViewProj",      view_proj.data());
    _gfx->uniform_m4("uInvViewProj",   inv_view_proj.data());
    _gfx->uniform_4f("uEye",           eye.x, eye.y, eye.z, 0.f);
    _gfx->uniform_2f("uViewport",      w, h);
    _gfx->uniform_1f("uHeaderH",       header_h);
    _gfx->uniform_1f("uSpacingMin",    sp_min);
    _gfx->uniform_1f("uSpacingMaj",    sp_maj);
    _gfx->uniform_1f("uFadeNear",      fade_near);
    _gfx->uniform_1f("uFadeFar",       fade_far);
    _gfx->uniform_4f("uBgColor",       VIEWPORT_BG.rf(),     VIEWPORT_BG.gf(),     VIEWPORT_BG.bf(),     1.f);
    _gfx->uniform_4f("uGridColor",     GRID_LINE.rf(),       GRID_LINE.gf(),       GRID_LINE.bf(),       1.f);
    _gfx->uniform_4f("uGridMajorColor",GRID_LINE_MAJOR.rf(), GRID_LINE_MAJOR.gf(), GRID_LINE_MAJOR.bf(), 1.f);
    _gfx->uniform_4f("uAxisXColor",    GRID_AXIS_X.rf(),     GRID_AXIS_X.gf(),     GRID_AXIS_X.bf(),     1.f);
    _gfx->uniform_4f("uAxisZColor",    GRID_AXIS_Y.rf(),     GRID_AXIS_Y.gf(),     GRID_AXIS_Y.bf(),     1.f);
    _gfx->draw_triangles(_vbo, LAYOUT_POS2, 0, 6);

    // Restore: disable depth test so the 2D UI draws on top unobstructed.
    _gfx->set_depth(false, false);
}

} // namespace bl_ui
