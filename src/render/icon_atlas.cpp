#include "icon_atlas.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <cmath>
#include <string>
#include <iostream>

// nanosvg — single-header, include exactly once here
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "../../third_party/nanosvg.h"
#include "../../third_party/nanosvgrast.h"

namespace bl_ui {

// ---------------------------------------------------------------------------
// Shader source (textured quad, RGBA tint multiply)
// ---------------------------------------------------------------------------

static const char* VERT_SRC = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform vec2 uVP;
out vec2 vUV;
void main(){
    vec2 ndc = aPos / uVP * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
}
)";

static const char* FRAG_SRC = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uTint;
out vec4 fragColor;
void main(){
    vec4 s = texture(uTex, vUV);
    // Icon SVGs rasterise as greyscale-in-alpha; colourize with tint.
    fragColor = vec4(uTint.rgb, uTint.a * s.a);
}
)";

// Vertex layout: (vec2 pos, vec2 uv) — stride 16, two attributes.
static const gfx::VertexAttr ATTRS[] = {
    {0, 2, 0},   // aPos
    {1, 2, 8},   // aUV
};
static const gfx::VertexLayout LAYOUT = { ATTRS, 2, 4 * sizeof(float) };

// ---------------------------------------------------------------------------
// Icon ID → SVG file stem mapping
// ---------------------------------------------------------------------------

const char* IconAtlas::_svg_stem(int id) {
    switch (id) {
        case ICON_NONE:              return nullptr;
        case ICON_CANCEL:            return "cancel";
        case ICON_ERROR:             return "error";
        case ICON_FILE_NEW:          return "file_new";
        case ICON_QUIT:              return "quit";
        case ICON_FILEBROWSER:       return "filebrowser";
        case ICON_FILE_FOLDER:       return "file_folder";
        case ICON_EXPORT:            return "export";
        case ICON_IMPORT:            return "import";
        case ICON_COPYDOWN:          return "copydown";
        case ICON_PASTEDOWN:         return "pastedown";
        case ICON_PREFERENCES:       return "preferences";
        case ICON_RENDER_STILL:      return "render_still";
        case ICON_RENDER_ANIMATION:  return "render_animation";
        case ICON_OUTPUT:            return "output";
        case ICON_FULLSCREEN_ENTER:  return "fullscreen_enter";
        case ICON_FULLSCREEN_EXIT:   return "fullscreen_exit";
        case ICON_WINDOW:            return "window";
        case ICON_HELP:              return "help";
        case ICON_URL:               return "url";
        case ICON_TRANSFORM_MOVE:    return "transform_move";
        case ICON_TRANSFORM_ROTATE:  return "transform_rotate";
        case ICON_TRANSFORM_SCALE:   return "transform_scale";
        case ICON_TRANSFORM_ALL:     return "transform_all";
        default:                     return nullptr;
    }
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool IconAtlas::init(gfx::Backend& gfx, const std::string& svg_dir,
                     float icon_px, float content_scale) {
    _gfx           = &gfx;
    _svg_dir       = svg_dir;
    _icon_px       = icon_px;
    _content_scale = content_scale;
    _phys_px       = static_cast<int>(std::ceil(icon_px * content_scale));

    int rows = (MAX_ICONS + COLS - 1) / COLS;
    _atlas_w = COLS * _phys_px;
    _atlas_h = rows * _phys_px;
    _atlas_px.assign((size_t)_atlas_w * _atlas_h * 4, 0);

    // Allocate the GPU texture (filled lazily per icon).
    _tex = gfx.create_texture(_atlas_w, _atlas_h,
                               gfx::PixelFormat::RGBA8,
                               gfx::FilterMode::Linear,
                               _atlas_px.data());
    if (!_tex) return false;

    // Dynamic VBO: up to VBO_CAPACITY verts (256 icons × 6 verts × vec4).
    _vbo = gfx.create_buffer(nullptr, (size_t)VBO_CAPACITY * 4 * sizeof(float), true);
    if (!_vbo) return false;

    _sh = gfx.create_shader(VERT_SRC, FRAG_SRC);
    return _sh != 0;
}

// ---------------------------------------------------------------------------
// _rasterise: parse+render one SVG into atlas slot
// ---------------------------------------------------------------------------

bool IconAtlas::_rasterise(int icon_id, const std::string& svg_path, int slot) {
    NSVGimage* img = nsvgParseFromFile(svg_path.c_str(), "px", 96.f);
    if (!img) {
        std::cerr << "[IconAtlas] nsvgParseFromFile failed: " << svg_path << "\n";
        return false;
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    int   phys  = _phys_px;
    float scale = (img->width > 0.f) ? float(phys) / img->width : 1.f;

    std::vector<unsigned char> raw(phys * phys * 4, 0);
    nsvgRasterize(rast, img, 0.f, 0.f, scale, raw.data(), phys, phys, phys * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);

    // Convert to alpha-only (white fill, alpha = coverage mask) in a
    // tightly-packed temp buffer — avoids row-stride complications when
    // uploading the sub-region via update_texture().
    std::vector<unsigned char> processed(phys * phys * 4);
    for (int i = 0; i < phys * phys; ++i) {
        processed[i * 4 + 0] = 255;
        processed[i * 4 + 1] = 255;
        processed[i * 4 + 2] = 255;
        processed[i * 4 + 3] = raw[i * 4 + 3];
    }

    // Upload sub-region (tightly packed, so no row-stride issues).
    int ax = (slot % COLS) * phys;
    int ay = (slot / COLS) * phys;
    _gfx->update_texture(_tex, ax, ay, phys, phys, processed.data());

    float u0 = float(ax)        / _atlas_w;
    float v0 = float(ay)        / _atlas_h;
    float u1 = float(ax + phys) / _atlas_w;
    float v1 = float(ay + phys) / _atlas_h;
    _cells[icon_id] = { u0, v0, u1, v1, true };

    return true;
}

// ---------------------------------------------------------------------------
// _load_icon
// ---------------------------------------------------------------------------

bool IconAtlas::_load_icon(int icon_id) {
    auto it = _cells.find(icon_id);
    if (it != _cells.end() && it->second.loaded) return true;

    const char* stem = _svg_stem(icon_id);
    if (!stem) return false;

    std::string path = _svg_dir + "/" + stem + ".svg";
    int slot = _next_slot++;
    if (slot >= MAX_ICONS) { std::cerr << "[IconAtlas] atlas full\n"; return false; }

    return _rasterise(icon_id, path, slot);
}

// ---------------------------------------------------------------------------
// draw_icon
// ---------------------------------------------------------------------------

void IconAtlas::draw_icon(int icon_id, float x, float y, RGBA tint) {
    if (!_tex || icon_id == ICON_NONE) return;
    if (!_load_icon(icon_id)) return;

    const Cell& c = _cells[icon_id];
    float w = _icon_px, h = _icon_px;

    if (_vbo_vertex_top + 6 > VBO_CAPACITY) _vbo_vertex_top = 0;
    int first_vertex = _vbo_vertex_top;
    _vbo_vertex_top += 6;

    float verts[6][4] = {
        {x,     y,     c.u0, c.v0},
        {x + w, y,     c.u1, c.v0},
        {x + w, y + h, c.u1, c.v1},
        {x,     y,     c.u0, c.v0},
        {x + w, y + h, c.u1, c.v1},
        {x,     y + h, c.u0, c.v1},
    };
    size_t byte_offset = (size_t)first_vertex * 4 * sizeof(float);
    _gfx->update_buffer(_vbo, verts, sizeof(verts), byte_offset);

    _gfx->set_blend_alpha(true);
    _gfx->use_shader(_sh);
    _gfx->uniform_2f("uVP",  _vp_w, _vp_h);
    _gfx->uniform_4f("uTint", tint.rf(), tint.gf(), tint.bf(), tint.af());
    _gfx->bind_texture(0, _tex, "uTex");
    _gfx->draw_triangles(_vbo, LAYOUT, first_vertex, 6);
}

} // namespace bl_ui
