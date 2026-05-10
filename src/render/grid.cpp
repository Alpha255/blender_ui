#include "grid.h"
#include "gl_context.h"
#include <cmath>
#include <algorithm>

namespace bl_ui {

// ---------------------------------------------------------------------------
// GLSL shaders
// ---------------------------------------------------------------------------

// Vertex shader — draws a full-viewport quad in screen-space coordinates
// (y=0 at top, same convention as the rest of the UI).  The fragment shader
// receives the logical-pixel position so it can compute world coordinates.
static const char* GRID_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;  // logical screen pixels (y=0 top)
uniform vec2 uViewport;             // logical viewport size
out vec2 vPos;                      // passed to fragment shader

void main() {
    vPos = aPos;
    float nx =  (aPos.x / uViewport.x) * 2.0 - 1.0;
    float ny = -(aPos.y / uViewport.y) * 2.0 + 1.0;
    gl_Position = vec4(nx, ny, 0.0, 1.0);
}
)GLSL";

// Fragment shader — analytical infinite grid.
//
// The fwidth()-based formula makes each grid line exactly ~1 screen pixel wide
// regardless of zoom.  Minor and major grid lines are blended on top of the
// background colour; axis lines (world x=0 and y=0) are drawn last with a
// distinct colour.
//
// uSpacing      — minor grid spacing in world units (computed on CPU)
// uMajorFactor  — major_spacing = uSpacing * uMajorFactor (typically 5)
static const char* GRID_FRAG = R"GLSL(
#version 330 core
in  vec2 vPos;
out vec4 fragColor;

uniform vec2  uViewport;
uniform vec2  uPan;         // screen pos of world origin (logical px)
uniform float uZoom;        // logical px per world unit
uniform float uHeaderH;     // px occupied by menu bar (grid hidden above)
uniform float uSpacing;     // minor grid spacing, world units
uniform float uMajorFactor; // major_spacing = uSpacing * uMajorFactor

uniform vec4 uBgColor;
uniform vec4 uGridColor;
uniform vec4 uGridMajorColor;
uniform vec4 uAxisXColor;   // world y=0 (horizontal line)
uniform vec4 uAxisYColor;   // world x=0 (vertical line)

// Returns [0,1] coverage of a 1-px wide grid line centred at every
// `spacing` world-units along `world_coord`.
// fwidth(c) gives the rate of change of c per screen pixel, so the
// smoothstep envelope is always 1 pixel wide in screen space.
float line_alpha(float world_coord, float spacing) {
    float c    = world_coord / spacing;
    float dfc  = max(fwidth(c), 1e-4);
    float dist = abs(fract(c + 0.5) - 0.5);   // 0 = on a line, 0.5 = halfway between
    return 1.0 - smoothstep(dfc * 0.4, dfc * 1.4, dist);
}

void main() {
    // Suppress grid in the menu-bar strip.
    if (vPos.y < uHeaderH) {
        fragColor = uBgColor;
        return;
    }

    // World coordinates of this fragment (y increases downward, matching screen).
    vec2 w = (vPos - uPan) / uZoom;

    float maj = uSpacing * uMajorFactor;

    // Minor grid: max of horizontal and vertical line coverages.
    float minor_a = max(line_alpha(w.x, uSpacing), line_alpha(w.y, uSpacing));

    // Major grid (overlaid on minor).
    float major_a = max(line_alpha(w.x, maj), line_alpha(w.y, maj));

    // Axis lines — 1.5 px wide in screen space, independent of zoom.
    float ax_a = 1.0 - smoothstep(0.0, 1.5, abs(vPos.y - uPan.y)); // world y=0
    float ay_a = 1.0 - smoothstep(0.0, 1.5, abs(vPos.x - uPan.x)); // world x=0

    // Composite: bg → minor → major → axes.
    vec4 col = uBgColor;
    col = mix(col, uGridColor,      minor_a * 0.7);
    col = mix(col, uGridMajorColor, major_a);
    col = mix(col, uAxisXColor,     ax_a);
    col = mix(col, uAxisYColor,     ay_a);

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

    _u_viewport     = glGetUniformLocation(_prog, "uViewport");
    _u_pan          = glGetUniformLocation(_prog, "uPan");
    _u_zoom         = glGetUniformLocation(_prog, "uZoom");
    _u_header_h     = glGetUniformLocation(_prog, "uHeaderH");
    _u_spacing      = glGetUniformLocation(_prog, "uSpacing");
    _u_major_factor = glGetUniformLocation(_prog, "uMajorFactor");
    _u_bg           = glGetUniformLocation(_prog, "uBgColor");
    _u_grid         = glGetUniformLocation(_prog, "uGridColor");
    _u_grid_major   = glGetUniformLocation(_prog, "uGridMajorColor");
    _u_axis_x       = glGetUniformLocation(_prog, "uAxisXColor");
    _u_axis_y       = glGetUniformLocation(_prog, "uAxisYColor");

    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    // 6 vertices × 2 floats (full-screen quad, updated each frame)
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

// Choose minor-grid world-unit spacing so that grid lines are roughly
// 80 logical pixels apart on screen.  Snaps to {1,2,5}×10^n increments
// to match the "round number" convention used in most 2D editors.
float Grid::_spacing(float zoom) {
    float world_target = 80.f / zoom;
    if (world_target <= 0.f) return 1.f;
    float power = std::pow(10.f, std::floor(std::log10(world_target)));
    float norm  = world_target / power;
    if      (norm > 5.f) return power * 10.f;
    else if (norm > 2.f) return power * 5.f;
    else if (norm > 1.f) return power * 2.f;
    return power;
}

void Grid::draw(float pan_x, float pan_y, float zoom, float header_h) {
    // Full-viewport quad covering (0,0)→(vp_w,vp_h) in screen coords.
    float w = _vp_w, h = _vp_h;
    float quad[12] = {
        0.f, 0.f,   w,  0.f,   w,  h,
        0.f, 0.f,   w,  h,    0.f, h,
    };
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad);

    float spacing = _spacing(zoom);

    using namespace Theme;
    glUseProgram(_prog);
    glUniform2f(_u_viewport,     w, h);
    glUniform2f(_u_pan,          pan_x, pan_y);
    glUniform1f(_u_zoom,         zoom);
    glUniform1f(_u_header_h,     header_h);
    glUniform1f(_u_spacing,      spacing);
    glUniform1f(_u_major_factor, 5.f);
    glUniform4f(_u_bg,           VIEWPORT_BG.rf(),      VIEWPORT_BG.gf(),      VIEWPORT_BG.bf(),      1.f);
    glUniform4f(_u_grid,         GRID_LINE.rf(),        GRID_LINE.gf(),        GRID_LINE.bf(),        1.f);
    glUniform4f(_u_grid_major,   GRID_LINE_MAJOR.rf(),  GRID_LINE_MAJOR.gf(),  GRID_LINE_MAJOR.bf(),  1.f);
    glUniform4f(_u_axis_x,       GRID_AXIS_X.rf(),      GRID_AXIS_X.gf(),      GRID_AXIS_X.bf(),      1.f);
    glUniform4f(_u_axis_y,       GRID_AXIS_Y.rf(),      GRID_AXIS_Y.gf(),      GRID_AXIS_Y.bf(),      1.f);

    glDisable(GL_BLEND); // grid is fully opaque; no blending needed
    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

} // namespace bl_ui
