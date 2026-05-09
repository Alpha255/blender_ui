#include "roundbox.h"
#include "gl_context.h"
#include <iostream>

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
uniform float uRadius;     // corner radius px
uniform vec4  uColorBg;    // inner fill (RGBA, premultiplied expected)
uniform vec4  uColorOut;   // outline color
uniform float uOutWidth;   // outline thickness px (0 = no outline)

float roundedRectSDF(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + r;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

void main() {
    vec2 p = (vUV - 0.5) * uSize;
    float d   = roundedRectSDF(p, uSize * 0.5, uRadius);
    float aa  = fwidth(d);
    float alpha = 1.0 - smoothstep(-aa, aa, d);

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
// Solid-color triangle shader (for submenu arrows)
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

// ---------------------------------------------------------------------------
// Roundbox
// ---------------------------------------------------------------------------

Roundbox::~Roundbox() {
    if (_vbo)      glDeleteBuffers(1, &_vbo);
    if (_vao)      glDeleteVertexArrays(1, &_vao);
    if (_prog)     glDeleteProgram(_prog);
    if (_tri_vbo)  glDeleteBuffers(1, &_tri_vbo);
    if (_tri_vao)  glDeleteVertexArrays(1, &_tri_vao);
    if (_tri_prog) glDeleteProgram(_tri_prog);
}

bool Roundbox::init() {
    _prog = compile_shader(VERT_SRC, FRAG_SRC);
    if (!_prog) return false;

    // Cache uniform locations
    _u_rect      = glGetUniformLocation(_prog, "uRect");
    _u_viewport  = glGetUniformLocation(_prog, "uViewport");
    _u_size      = glGetUniformLocation(_prog, "uSize");
    _u_radius    = glGetUniformLocation(_prog, "uRadius");
    _u_color_bg  = glGetUniformLocation(_prog, "uColorBg");
    _u_color_out = glGetUniformLocation(_prog, "uColorOut");
    _u_out_width = glGetUniformLocation(_prog, "uOutWidth");

    // Unit quad: two triangles covering [0,1]^2
    float quad[] = {
        0.f, 0.f,   1.f, 0.f,   1.f, 1.f,
        0.f, 0.f,   1.f, 1.f,   0.f, 1.f,
    };

    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);

    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    // Triangle renderer init
    _tri_prog    = compile_shader(TRI_VERT, TRI_FRAG);
    _tri_u_vp    = glGetUniformLocation(_tri_prog, "uViewport");
    _tri_u_color = glGetUniformLocation(_tri_prog, "uColor");

    glGenVertexArrays(1, &_tri_vao);
    glGenBuffers(1, &_tri_vbo);
    glBindVertexArray(_tri_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _tri_vbo);
    glBufferData(GL_ARRAY_BUFFER, 3 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    return _tri_prog != 0;
}

void Roundbox::set_viewport(float width, float height) {
    _vp_w = width;
    _vp_h = height;
}

void Roundbox::draw_roundbox(float x, float y, float w, float h,
                              float radius,
                              RGBA fill, RGBA outline, float outline_w) {
    glUseProgram(_prog);

    glUniform4f(_u_rect,      x, y, w, h);
    glUniform2f(_u_viewport,  _vp_w, _vp_h);
    glUniform2f(_u_size,      w, h);
    glUniform1f(_u_radius,    radius);
    glUniform4f(_u_color_bg,  fill.rf(),    fill.gf(),    fill.bf(),    fill.af());
    glUniform4f(_u_color_out, outline.rf(), outline.gf(), outline.bf(), outline.af());
    glUniform1f(_u_out_width, outline_w);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Roundbox::draw_rect_filled(float x, float y, float w, float h, RGBA color) {
    draw_roundbox(x, y, w, h, 0.f, color);
}

void Roundbox::draw_line_h(float x, float y, float len, RGBA color) {
    draw_roundbox(x, y, len, 1.f, 0.f, color);
}

void Roundbox::draw_line_v(float x, float y, float h, RGBA color) {
    draw_roundbox(x, y, 1.f, h, 0.f, color);
}

void Roundbox::draw_triangle_right(float x, float y, float w, float h, RGBA color) {
    // Right-pointing triangle: top-left, bottom-left, right midpoint.
    // Matches Blender's widget_draw_submenu_tria / draw_anti_tria_rect().
    float verts[3][2] = {
        {x,     y},
        {x,     y + h},
        {x + w, y + h * 0.5f},
    };

    glUseProgram(_tri_prog);
    glUniform2f(_tri_u_vp,    _vp_w, _vp_h);
    glUniform4f(_tri_u_color, color.rf(), color.gf(), color.bf(), color.af());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(_tri_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _tri_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

} // namespace bl_ui
