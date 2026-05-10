#include "grid.h"
#include "gl_context.h"
#include <cmath>
#include <algorithm>

namespace bl_ui {

// ---------------------------------------------------------------------------
// GLSL shaders
// ---------------------------------------------------------------------------

// Vertex: full-screen quad in logical screen pixels (y=0 at top).
// The quad covers (0,0)→(vp_w, vp_h) so vPos gives the logical screen
// position; the fragment shader uses it to derive NDC for unprojection.
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

// Fragment: perspective ray-plane intersection grid.
//
// Each fragment unprojects its NDC position via uInvViewProj to get a world
// ray, intersects y=0 ground plane, then evaluates the grid analytically.
// fwidth() on the world coordinate naturally handles perspective foreshortening.
static const char* GRID_FRAG = R"GLSL(
#version 330 core
in  vec2 vPos;          // logical screen px, y=0 at top
out vec4 fragColor;

uniform mat4  uViewProj;
uniform mat4  uInvViewProj;
uniform vec3  uEye;
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
// Returns 1 at line centres, 0 between lines, ~1px transition.
float line_alpha(float world_coord, float spacing) {
    float c   = world_coord / spacing;
    float dfc = max(fwidth(c), 1e-5);
    float d   = abs(fract(c + 0.5) - 0.5);  // 0 on line, 0.5 midway
    return 1.0 - smoothstep(dfc * 0.5, dfc * 1.5, d);
}

void main() {
    // Default depth for fragments that don't hit the ground plane.
    // gl_FragDepth must be written in every execution path when used at all.
    gl_FragDepth = 1.0;

    // Suppress the menu-bar strip (y=0 is top of window).
    if (vPos.y < uHeaderH) {
        fragColor = uBgColor;
        return;
    }

    // NDC of this fragment (logical screen → NDC, y flipped).
    vec2 ndc = vec2(
        vPos.x / uViewport.x * 2.0 - 1.0,
        1.0 - vPos.y / uViewport.y * 2.0
    );

    // Unproject near/far clip-plane points → world space via homogeneous div.
    vec4 nh = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 fh = uInvViewProj * vec4(ndc,  1.0, 1.0);
    vec3 near_w = nh.xyz / nh.w;
    vec3 far_w  = fh.xyz / fh.w;
    vec3 ray    = far_w - near_w;   // world-space ray direction (unnormalized)

    // Intersect with y=0 ground plane: near_w.y + t * ray.y = 0 → t = ...
    if (abs(ray.y) < 1e-6) { fragColor = uBgColor; return; }
    float t = -near_w.y / ray.y;
    if (t < 0.0) { fragColor = uBgColor; return; }   // behind camera

    vec3 hit = near_w + t * ray;    // world-space hit on y=0

    // Overwrite with correct depth for this ground-plane hit point.
    vec4 clip = uViewProj * vec4(hit, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

    // Grid line coverages at hit point.
    float minor_a = max(line_alpha(hit.x, uSpacingMin),
                        line_alpha(hit.z, uSpacingMin));
    float major_a = max(line_alpha(hit.x, uSpacingMaj),
                        line_alpha(hit.z, uSpacingMaj));

    // Axis lines: X axis is the z=0 line, Z axis is the x=0 line.
    // Use fwidth of the axis coordinate for consistent 1-px screen width.
    float axX_a = 1.0 - smoothstep(0.0, fwidth(hit.z) * 2.0, abs(hit.z));
    float axZ_a = 1.0 - smoothstep(0.0, fwidth(hit.x) * 2.0, abs(hit.x));

    // Distance fade (linear → smooth falloff).
    float dist  = length(hit - uEye);
    float fade  = 1.0 - smoothstep(uFadeNear, uFadeFar, dist);

    // Composite: bg → minor → major → X-axis → Z-axis.
    vec4 col = uBgColor;
    col = mix(col, uGridColor,      minor_a * 0.7 * fade);
    col = mix(col, uGridMajorColor, major_a       * fade);
    col = mix(col, uAxisXColor,     axX_a         * fade);
    col = mix(col, uAxisZColor,     axZ_a         * fade);

    fragColor = col;
}
)GLSL";

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

Grid::~Grid() {
    if (_vbo)  glDeleteBuffers(1, &_vbo);
    if (_vao)  glDeleteVertexArrays(1, &_vao);
    if (_prog) glDeleteProgram(_prog);
}

bool Grid::init() {
    _prog = compile_shader(GRID_VERT, GRID_FRAG);
    if (!_prog) return false;

    _u_view_proj   = glGetUniformLocation(_prog, "uViewProj");
    _u_inv_vp      = glGetUniformLocation(_prog, "uInvViewProj");
    _u_eye         = glGetUniformLocation(_prog, "uEye");
    _u_viewport    = glGetUniformLocation(_prog, "uViewport");
    _u_header_h    = glGetUniformLocation(_prog, "uHeaderH");
    _u_spacing_min = glGetUniformLocation(_prog, "uSpacingMin");
    _u_spacing_maj = glGetUniformLocation(_prog, "uSpacingMaj");
    _u_fade_near   = glGetUniformLocation(_prog, "uFadeNear");
    _u_fade_far    = glGetUniformLocation(_prog, "uFadeFar");
    _u_bg          = glGetUniformLocation(_prog, "uBgColor");
    _u_grid        = glGetUniformLocation(_prog, "uGridColor");
    _u_grid_major  = glGetUniformLocation(_prog, "uGridMajorColor");
    _u_axis_x      = glGetUniformLocation(_prog, "uAxisXColor");
    _u_axis_z      = glGetUniformLocation(_prog, "uAxisZColor");

    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    return true;
}

void Grid::set_viewport(float w, float h) {
    _vp_w = w;
    _vp_h = h;
}

// Snap to {1, 2, 5}×10^n so that minor spacing is ~10% of camera distance.
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
                const Vec3& eye, float distance, float header_h)
{
    float w = _vp_w, h = _vp_h;
    // Full-screen quad in logical screen coordinates.
    float quad[12] = {
        0.f, 0.f,   w,  0.f,   w,  h,
        0.f, 0.f,   w,  h,    0.f, h,
    };
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad);

    float sp_min = _spacing(distance);
    float sp_maj = sp_min * 10.f;

    // Fade lines over 5×–20× camera distance.
    float fade_near = distance * 5.f;
    float fade_far  = distance * 20.f;

    using namespace Theme;
    glUseProgram(_prog);
    glUniformMatrix4fv(_u_view_proj, 1, GL_FALSE, view_proj.data());
    glUniformMatrix4fv(_u_inv_vp,    1, GL_FALSE, inv_view_proj.data());
    glUniform3f(_u_eye,         eye.x, eye.y, eye.z);
    glUniform2f(_u_viewport,    w, h);
    glUniform1f(_u_header_h,    header_h);
    glUniform1f(_u_spacing_min, sp_min);
    glUniform1f(_u_spacing_maj, sp_maj);
    glUniform1f(_u_fade_near,   fade_near);
    glUniform1f(_u_fade_far,    fade_far);
    glUniform4f(_u_bg,          VIEWPORT_BG.rf(),      VIEWPORT_BG.gf(),      VIEWPORT_BG.bf(),      1.f);
    glUniform4f(_u_grid,        GRID_LINE.rf(),        GRID_LINE.gf(),        GRID_LINE.bf(),        1.f);
    glUniform4f(_u_grid_major,  GRID_LINE_MAJOR.rf(),  GRID_LINE_MAJOR.gf(),  GRID_LINE_MAJOR.bf(),  1.f);
    glUniform4f(_u_axis_x,      GRID_AXIS_X.rf(),      GRID_AXIS_X.gf(),      GRID_AXIS_X.bf(),      1.f);
    glUniform4f(_u_axis_z,      GRID_AXIS_Y.rf(),      GRID_AXIS_Y.gf(),      GRID_AXIS_Y.bf(),      1.f);

    // Grid renders into depth buffer so future 3D geometry composites correctly.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);   // always write, no depth rejection on this pass
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Restore state: leave depth test disabled so 2D UI draws on top unobstructed.
    // Callers that want to draw 3D geometry after this can re-enable depth test.
    glDepthFunc(GL_LESS);
    glDisable(GL_DEPTH_TEST);
}

} // namespace bl_ui
