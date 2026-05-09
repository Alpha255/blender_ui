#include "font.h"
#include "gl_context.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <iostream>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace bl_ui {

// ---------------------------------------------------------------------------
// GLSL shaders
// ---------------------------------------------------------------------------

static const char* TEXT_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec4 aVertex; // (x, y, u, v)
uniform mat4 uProj;
out vec2 vTex;
void main() {
    vTex = aVertex.zw;
    gl_Position = uProj * vec4(aVertex.xy, 0.0, 1.0);
}
)GLSL";

static const char* TEXT_FRAG = R"GLSL(
#version 330 core
in  vec2 vTex;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec4      uColor;
void main() {
    float a = texture(uTex, vTex).r;
    fragColor = vec4(uColor.rgb, uColor.a * a);
}
)GLSL";

// ---------------------------------------------------------------------------
// Font search paths
// ---------------------------------------------------------------------------

static const char* FONT_PATHS[] = {
    "E:/SourceCode/Graph/blender_ui/assets/fonts/Inter.ttf",
    "E:/SourceCode/Graph/blender_ui/assets/fonts/DejaVuSansMono.ttf",
    "E:/SourceCode/Graph/blender_ui/assets/fonts/inter_zip/extras/ttf/Inter-Regular.ttf",
    "E:/SourceCode/Graph/blender_ui/assets/fonts/Inter-Regular.ttf",
    "assets/fonts/inter_zip/extras/ttf/Inter-Regular.ttf",
    "assets/fonts/Inter-Regular.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/tahoma.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    nullptr,
};

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

Font::~Font() {
    if (_vbo)   glDeleteBuffers(1, &_vbo);
    if (_vao)   glDeleteVertexArrays(1, &_vao);
    if (_atlas) glDeleteTextures(1, &_atlas);
    if (_prog)  glDeleteProgram(_prog);
}

// _find_char: return packed metrics for a Unicode codepoint, nullptr if absent.
const Font::BakedChar* Font::_find_char(unsigned cp) const {
    if (cp >= 32u && cp < 128u) return &_chars[cp - 32];
    for (int i = 0; i < _extra_count; ++i)
        if (_extra[i].codepoint == cp) return &_extra[i].bc;
    return nullptr;
}

// ---------------------------------------------------------------------------
// _bake — pack API with 2× horizontal oversampling for crisper glyphs.
//         All stored metrics are in PHYSICAL pixels.
// ---------------------------------------------------------------------------

bool Font::_bake(const char* path, float px_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> ttf(sz);
    fread(ttf.data(), 1, sz, f);
    fclose(f);

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf.data(),
                        stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
        return false;

    std::vector<unsigned char> bitmap(_atlas_w * _atlas_h, 0);

    stbtt_pack_context spc;
    stbtt_PackBegin(&spc, bitmap.data(), _atlas_w, _atlas_h, 0, 1, nullptr);
    stbtt_PackSetOversampling(&spc, 2, 2); // 2×2 oversampling → better diagonal AA

    // ASCII 32-127
    stbtt_pack_range ascii_rng{};
    ascii_rng.font_size                        = px_size;
    ascii_rng.first_unicode_codepoint_in_range = 32;
    ascii_rng.num_chars                        = 96;
    ascii_rng.chardata_for_range               =
        reinterpret_cast<stbtt_packedchar*>(_chars);

    // Extra UI glyphs (▶ U+25B6 — submenu arrow)
    static const int EXTRA_CPS[] = { 0x25B6 };
    stbtt_packedchar extra_packed[EXTRA_MAX] = {};
    std::vector<int> valid_extra;
    for (int cp : EXTRA_CPS)
        if (stbtt_FindGlyphIndex(&info, cp) > 0)
            valid_extra.push_back(cp);

    stbtt_pack_range extra_rng{};
    if (!valid_extra.empty()) {
        extra_rng.font_size                        = px_size;
        extra_rng.array_of_unicode_codepoints      = valid_extra.data();
        extra_rng.num_chars                        = (int)valid_extra.size();
        extra_rng.chardata_for_range               = extra_packed;
    }

    stbtt_pack_range ranges[2] = { ascii_rng };
    int n_ranges = 1;
    if (!valid_extra.empty()) { ranges[1] = extra_rng; n_ranges = 2; }

    stbtt_PackFontRanges(&spc, ttf.data(), 0, ranges, n_ranges);
    stbtt_PackEnd(&spc);

    // Binary-layout check: BakedChar must match stbtt_packedchar exactly.
    static_assert(sizeof(BakedChar) == sizeof(stbtt_packedchar),
                  "BakedChar layout mismatch with stbtt_packedchar");

    _extra_count = 0;
    for (int i = 0; i < (int)valid_extra.size() && _extra_count < EXTRA_MAX; ++i) {
        _extra[_extra_count].codepoint = (unsigned)valid_extra[i];
        memcpy(&_extra[_extra_count].bc, &extra_packed[i], sizeof(BakedChar));
        ++_extra_count;
    }

    // Font metrics — stored in PHYSICAL pixels.
    float scale = stbtt_ScaleForPixelHeight(&info, px_size);
    int asc, desc, lgap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &lgap);
    _ascent = asc * scale;                    // physical — kept as-is
    _line_h = (asc - desc + lgap) * scale;   // physical — divided in load()

    glGenTextures(1, &_atlas);
    glBindTexture(GL_TEXTURE_2D, _atlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                 _atlas_w, _atlas_h, 0,
                 GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Font::_setup_gl() {
    _prog = compile_shader(TEXT_VERT, TEXT_FRAG);
    _u_proj  = glGetUniformLocation(_prog, "uProj");
    _u_tex   = glGetUniformLocation(_prog, "uTex");
    _u_color = glGetUniformLocation(_prog, "uColor");

    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 4096 * 6 * 4 * sizeof(float),
                 nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

bool Font::load(float size_pt, float dpi, float content_scale) {
    _content_scale = (content_scale > 0.f) ? content_scale : 1.f;
    float px_size  = size_pt * dpi * _content_scale / 72.f;

    for (int i = 0; FONT_PATHS[i]; ++i) {
        if (_bake(FONT_PATHS[i], px_size)) {
            std::cerr << "[bl_ui] Font loaded: " << FONT_PATHS[i]
                      << "  px=" << px_size << "\n";
            _setup_gl();
            if (_prog) {
                // _ascent stays in physical pixels (used internally by draw_text).
                // _line_h exposed to callers in logical pixels.
                _line_h /= _content_scale;
                return true;
            }
            glDeleteTextures(1, &_atlas); _atlas = 0;
        }
    }
    std::cerr << "[bl_ui] Font::load — no usable TTF font found\n";
    return false;
}

// ---------------------------------------------------------------------------
// UTF-8 decode helper (inline, reused in draw_text and measure_text)
// Returns codepoint and advances i by the sequence length.
// ---------------------------------------------------------------------------

static inline unsigned utf8_decode(std::string_view text, std::size_t& i) {
    unsigned cp  = (unsigned char)text[i];
    int      len = 1;
    if      ((cp & 0xE0) == 0xC0 && i+1 < text.size())
        { cp = ((cp & 0x1F) << 6)  | ((unsigned char)text[i+1] & 0x3F); len = 2; }
    else if ((cp & 0xF0) == 0xE0 && i+2 < text.size())
        { cp = ((cp & 0x0F) << 12) | (((unsigned char)text[i+1] & 0x3F) << 6)
                                    |  ((unsigned char)text[i+2] & 0x3F); len = 3; }
    else if ((cp & 0xF8) == 0xF0 && i+3 < text.size())
        { cp = ((cp & 0x07) << 18) | (((unsigned char)text[i+1] & 0x3F) << 12)
                                    | (((unsigned char)text[i+2] & 0x3F) << 6)
                                    |  ((unsigned char)text[i+3] & 0x3F); len = 4; }
    i += len;
    return cp;
}

// ---------------------------------------------------------------------------
// draw_text — works entirely in PHYSICAL pixels to avoid float-roundtrip blur.
//
// Key design:
//   • Input x,y are LOGICAL pixels (same coord space as the rest of the UI).
//   • We convert to physical at the start, keeping full sub-pixel precision
//     horizontally (2× h-oversampling handles it) and rounding vertically
//     (1× v-resolution — rounding prevents inter-line blur).
//   • The projection matrix maps physical pixels → NDC, matching glViewport
//     which is set to the physical framebuffer size in app.cpp.
// ---------------------------------------------------------------------------

void Font::draw_text(std::string_view text, float x, float y,
                     RGBA color, TextAlign align) {
    if (!ready() || text.empty()) return;

    if (align != TextAlign::LEFT) {
        float w = measure_text(text);
        if (align == TextAlign::CENTER) x -= w * 0.5f;
        else                            x -= w;
    }

    std::vector<float> verts;
    verts.reserve(text.size() * 6 * 4);

    float cs = _content_scale;
    // Horizontal: keep sub-pixel precision (2× h-oversampling uses it).
    // Vertical:   snap to physical pixel boundary to avoid v-blur.
    float cx       = x * cs;
    float baseline = std::round(y * cs) + _ascent; // _ascent is physical px

    for (std::size_t i = 0; i < text.size(); ) {
        unsigned cp = utf8_decode(text, i);
        const BakedChar* bc = _find_char(cp);
        if (!bc) continue;

        // xoff/xoff2, yoff/yoff2 are physical-pixel offsets set by the pack API.
        float qx0 = cx + bc->xoff;
        float qy0 = baseline + bc->yoff;
        float qx1 = cx + bc->xoff2;
        float qy1 = baseline + bc->yoff2;

        float qs0 = bc->x0 / float(_atlas_w);
        float qt0 = bc->y0 / float(_atlas_h);
        float qs1 = bc->x1 / float(_atlas_w);
        float qt1 = bc->y1 / float(_atlas_h);

        verts.insert(verts.end(), {qx0,qy0,qs0,qt0,  qx1,qy0,qs1,qt0,  qx1,qy1,qs1,qt1});
        verts.insert(verts.end(), {qx0,qy0,qs0,qt0,  qx1,qy1,qs1,qt1,  qx0,qy1,qs0,qt1});

        cx += bc->xadvance; // advance in physical px, no division needed
    }

    if (verts.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    verts.size() * sizeof(float), verts.data());

    // Project physical pixels → NDC.  _vp_w/_vp_h are logical; multiply by cs
    // to get physical dimensions that match the glViewport set in app.cpp.
    float pw = _vp_w * cs;
    float ph = _vp_h * cs;
    float proj[16] = {
         2.f/pw,    0.f,    0.f, 0.f,
         0.f,   -2.f/ph,   0.f, 0.f,
         0.f,       0.f,  -1.f, 0.f,
        -1.f,       1.f,   0.f, 1.f,
    };

    glUseProgram(_prog);
    glUniformMatrix4fv(_u_proj, 1, GL_FALSE, proj);
    glUniform1i(_u_tex, 0);
    glUniform4f(_u_color, color.rf(), color.gf(), color.bf(), color.af());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _atlas);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 4));
    glBindVertexArray(0);
}

float Font::measure_text(std::string_view text) const {
    float w = 0.f;
    for (std::size_t i = 0; i < text.size(); ) {
        unsigned cp = utf8_decode(text, i);
        const BakedChar* bc = _find_char(cp);
        if (bc) w += bc->xadvance;
    }
    return w / _content_scale; // physical → logical
}

} // namespace bl_ui
