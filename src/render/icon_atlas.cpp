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

static GLuint compile_prog(const char* vert, const char* frag) {
    auto compile = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512]; glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
            std::cerr << "[IconAtlas] shader error: " << buf << "\n";
        }
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, vert);
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// ---------------------------------------------------------------------------
// Mapping: icon_id → SVG file stem
// Only the icons we actually expose in bl_ui/icons.h.
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
        default:                     return nullptr;
    }
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool IconAtlas::init(const std::string& svg_dir, float icon_px, float content_scale) {
    _svg_dir       = svg_dir;
    _icon_px       = icon_px;
    _content_scale = content_scale;
    _phys_px       = static_cast<int>(std::ceil(icon_px * content_scale));

    // Atlas texture dimensions: COLS columns, enough rows for MAX_ICONS
    int rows    = (MAX_ICONS + COLS - 1) / COLS;
    _atlas_w    = COLS * _phys_px;
    _atlas_h    = rows * _phys_px;
    _atlas_px.assign(static_cast<size_t>(_atlas_w) * _atlas_h * 4, 0);

    // GL texture (upload will happen lazily per icon)
    glGenTextures(1, &_tex);
    glBindTexture(GL_TEXTURE_2D, _tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 _atlas_w, _atlas_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, _atlas_px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    // VAO/VBO for a unit quad (6 verts, position+UV)
    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    _prog = compile_prog(VERT_SRC, FRAG_SRC);
    return _prog != 0;
}

// ---------------------------------------------------------------------------
// _rasterise: parse+render one SVG into atlas slot
// ---------------------------------------------------------------------------

bool IconAtlas::_rasterise(int icon_id, const std::string& svg_path, int slot) {
    // Parse
    NSVGimage* img = nsvgParseFromFile(svg_path.c_str(), "px", 96.f);
    if (!img) {
        std::cerr << "[IconAtlas] nsvgParseFromFile failed: " << svg_path << "\n";
        return false;
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();

    // Rasterise at physical pixel size
    int    phys  = _phys_px;
    float  scale = (img->width > 0.f) ? float(phys) / img->width : 1.f;
    std::vector<unsigned char> tmp(phys * phys * 4, 0);
    nsvgRasterize(rast, img, 0.f, 0.f, scale, tmp.data(), phys, phys, phys * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);

    // Copy pixels into atlas CPU buffer (with proper atlas row stride).
    int ax = (slot % COLS) * phys;
    int ay = (slot / COLS) * phys;
    for (int row = 0; row < phys; ++row) {
        unsigned char* dst = _atlas_px.data() + ((ay + row) * _atlas_w + ax) * 4;
        const unsigned char* src = tmp.data() + row * phys * 4;
        // Icons are filled with #fff (white) — use alpha channel as the coverage mask.
        // The shader applies the tint colour; RGB in the atlas is always 255.
        for (int col = 0; col < phys; ++col) {
            dst[col * 4 + 0] = 255;
            dst[col * 4 + 1] = 255;
            dst[col * 4 + 2] = 255;
            dst[col * 4 + 3] = src[col * 4 + 3];
        }
    }

    // Upload to GPU.
    // GL_UNPACK_ROW_LENGTH must match the atlas CPU buffer width so that OpenGL
    // reads the correct row stride (atlas_w px) rather than the sub-image width (phys px).
    glBindTexture(GL_TEXTURE_2D, _tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, _atlas_w);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    ax, ay, phys, phys,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    _atlas_px.data() + (ay * _atlas_w + ax) * 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);  // restore default
    glBindTexture(GL_TEXTURE_2D, 0);

    // Register UV coords
    float u0 = float(ax)         / _atlas_w;
    float v0 = float(ay)         / _atlas_h;
    float u1 = float(ax + phys)  / _atlas_w;
    float v1 = float(ay + phys)  / _atlas_h;
    _cells[icon_id] = {u0, v0, u1, v1, true};

    return true;
}

// ---------------------------------------------------------------------------
// _load_icon: on-demand load for a given icon_id
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
    float w = _icon_px;
    float h = _icon_px;

    // 6 vertices: position (x,y) + UV
    float verts[6][4] = {
        {x,     y,     c.u0, c.v0},
        {x + w, y,     c.u1, c.v0},
        {x + w, y + h, c.u1, c.v1},
        {x,     y,     c.u0, c.v0},
        {x + w, y + h, c.u1, c.v1},
        {x,     y + h, c.u0, c.v1},
    };

    glUseProgram(_prog);
    glUniform2f(glGetUniformLocation(_prog, "uVP"), _vp_w, _vp_h);
    glUniform4f(glGetUniformLocation(_prog, "uTint"),
                tint.rf(), tint.gf(), tint.bf(), tint.af());
    glUniform1i(glGetUniformLocation(_prog, "uTex"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _tex);

    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

IconAtlas::~IconAtlas() {
    if (_tex)  { glDeleteTextures(1, &_tex);      _tex  = 0; }
    if (_vao)  { glDeleteVertexArrays(1, &_vao);  _vao  = 0; }
    if (_vbo)  { glDeleteBuffers(1, &_vbo);        _vbo  = 0; }
    if (_prog) { glDeleteProgram(_prog);           _prog = 0; }
}

} // namespace bl_ui
