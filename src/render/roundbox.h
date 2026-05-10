#pragma once
#include <GL/glew.h>
#include "theme.h"

namespace bl_ui {

// ---------------------------------------------------------------------------
// Roundbox — draws rounded rectangles using an SDF fragment shader.
// Replaces Blender's GPU_SHADER_2D_WIDGET_BASE rounded corner draw.
// Source ref: source/blender/editors/interface/interface_widgets.cc:5832
// ---------------------------------------------------------------------------

class Roundbox {
public:
    Roundbox() = default;
    ~Roundbox();

    // Must be called with an active OpenGL context.
    bool init();

    // Set current viewport size (call on framebuffer resize).
    void set_viewport(float width, float height);

    // Draw a rounded rectangle with a uniform corner radius.
    // Coords are in screen pixels, y=0 at top.  outline_w=0 means no outline.
    void draw_roundbox(float x, float y, float w, float h,
                       float radius,
                       RGBA  fill,
                       RGBA  outline   = RGBA{0,0,0,0},
                       float outline_w = 0.f);

    // Per-corner variant — matches Blender's roundboxalign corner flags.
    // r_tl/r_tr/r_bl/r_br = top-left, top-right, bottom-left, bottom-right radii in px.
    void draw_roundbox(float x, float y, float w, float h,
                       float r_tl, float r_tr, float r_bl, float r_br,
                       RGBA  fill,
                       RGBA  outline   = RGBA{0,0,0,0},
                       float outline_w = 0.f);

    // Draw a solid filled rectangle (no rounding).
    void draw_rect_filled(float x, float y, float w, float h, RGBA color);

    // Draw a horizontal line from (x, y) of length len.
    void draw_line_h(float x, float y, float len, RGBA color);

    // Draw a vertical line from (x, y) of height h.
    void draw_line_v(float x, float y, float h, RGBA color);

    // Draw a right-pointing filled triangle — used for submenu arrows.
    // Matches Blender's widget_draw_submenu_tria() / draw_anti_tria_rect().
    // (x,y) = top-left of bounding box; w,h = dimensions in logical pixels.
    void draw_triangle_right(float x, float y, float w, float h, RGBA color);

    // Draw a thick line segment from (x0,y0) to (x1,y1) with given pixel width.
    void draw_line_segment(float x0, float y0, float x1, float y1,
                           float width, RGBA color);

private:
    // Rounded-rect SDF shader
    GLuint _prog    = 0;
    GLuint _vao     = 0;
    GLuint _vbo     = 0;
    GLint _u_rect      = -1;
    GLint _u_viewport  = -1;
    GLint _u_size      = -1;
    GLint _u_radius4   = -1;  // vec4: top-left, top-right, bottom-left, bottom-right
    GLint _u_color_bg  = -1;
    GLint _u_color_out = -1;
    GLint _u_out_width = -1;

    // Simple solid-color triangle shader (for submenu arrow)
    GLuint _tri_prog  = 0;
    GLuint _tri_vao   = 0;
    GLuint _tri_vbo   = 0;
    GLint  _tri_u_vp    = -1;
    GLint  _tri_u_color = -1;

    float _vp_w = 800.f, _vp_h = 600.f;
};

} // namespace bl_ui
