#include "font.h"
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
layout(location = 0) in vec4 aVertex; // (screen_x, screen_y, texel_u, texel_v)
uniform mat4 uProj;
out vec2 vTexel;  // atlas texel-space coordinates (0..atlas_w, 0..atlas_h)
void main() {
    vTexel = aVertex.zw;
    gl_Position = uProj * vec4(aVertex.xy, 0.0, 1.0);
}
)GLSL";

// Manual bilinear filtering — mirrors Blender's gpu_shader_text_frag.glsl.
// Uses texelFetch (integer pixel lookup) instead of texture() so the GPU
// sampler never blends across glyph atlas boundaries.
// vTexel is in atlas pixel space: 0 = left edge of pixel 0, integer N = boundary
// between pixels N-1 and N.  fract(vTexel - 0.5) gives bilinear sub-pixel weights.
static const char* TEXT_FRAG = R"GLSL(
#version 330 core
in  vec2 vTexel;
out vec4 fragColor;
uniform sampler2D uTex;       // glyph atlas (GL_NEAREST, single-channel)
uniform ivec2     uAtlasSize; // atlas dimensions (512, 512)
uniform vec4      uColor;
void main() {
    vec2  f = fract(vTexel - 0.5);
    ivec2 t = ivec2(floor(vTexel - 0.5));
    ivec2 sz = uAtlasSize - ivec2(1);
    float tl = texelFetch(uTex, clamp(t,               ivec2(0), sz), 0).r;
    float tr = texelFetch(uTex, clamp(t + ivec2(1, 0), ivec2(0), sz), 0).r;
    float bl = texelFetch(uTex, clamp(t + ivec2(0, 1), ivec2(0), sz), 0).r;
    float br = texelFetch(uTex, clamp(t + ivec2(1, 1), ivec2(0), sz), 0).r;
    float a  = mix(mix(tl, tr, f.x), mix(bl, br, f.x), f.y);
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

// Vertex layout: vec4 (screen_x, screen_y, texel_u, texel_v) at location 0.
static const gfx::VertexAttr ATTR_TEXT[] = { {0, 4, 0} };
static const gfx::VertexLayout LAYOUT_TEXT = { ATTR_TEXT, 1, 4 * sizeof(float) };

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

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

    _atlas_bitmap.assign((size_t)_atlas_w * _atlas_h, 0);

    stbtt_pack_context spc;
    stbtt_PackBegin(&spc, _atlas_bitmap.data(), _atlas_w, _atlas_h, 0, 1, nullptr);
    stbtt_PackSetOversampling(&spc, 2, 2);

    stbtt_pack_range ascii_rng{};
    ascii_rng.font_size                        = px_size;
    ascii_rng.first_unicode_codepoint_in_range = 32;
    ascii_rng.num_chars                        = 96;
    ascii_rng.chardata_for_range               =
        reinterpret_cast<stbtt_packedchar*>(_chars);

    static const int EXTRA_CPS[] = { 0x25B6 };
    stbtt_packedchar extra_packed[EXTRA_MAX] = {};
    std::vector<int> valid_extra;
    for (int cp : EXTRA_CPS)
        if (stbtt_FindGlyphIndex(&info, cp) > 0)
            valid_extra.push_back(cp);

    stbtt_pack_range extra_rng{};
    if (!valid_extra.empty()) {
        extra_rng.font_size                   = px_size;
        extra_rng.array_of_unicode_codepoints = valid_extra.data();
        extra_rng.num_chars                   = (int)valid_extra.size();
        extra_rng.chardata_for_range          = extra_packed;
    }

    stbtt_pack_range ranges[2] = { ascii_rng };
    int n_ranges = 1;
    if (!valid_extra.empty()) { ranges[1] = extra_rng; n_ranges = 2; }

    stbtt_PackFontRanges(&spc, ttf.data(), 0, ranges, n_ranges);
    stbtt_PackEnd(&spc);

    static_assert(sizeof(BakedChar) == sizeof(stbtt_packedchar),
                  "BakedChar layout mismatch with stbtt_packedchar");

    _extra_count = 0;
    for (int i = 0; i < (int)valid_extra.size() && _extra_count < EXTRA_MAX; ++i) {
        _extra[_extra_count].codepoint = (unsigned)valid_extra[i];
        memcpy(&_extra[_extra_count].bc, &extra_packed[i], sizeof(BakedChar));
        ++_extra_count;
    }

    float scale = stbtt_ScaleForPixelHeight(&info, px_size);
    int asc, desc, lgap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &lgap);
    _ascent = asc * scale;
    _line_h = (asc - desc + lgap) * scale;

    return true;
}

void Font::_setup_gfx(gfx::Backend& gfx) {
    _gfx  = &gfx;
    _sh   = gfx.create_shader(TEXT_VERT, TEXT_FRAG);

    // Upload atlas with GL_NEAREST (manual bilinear in shader).
    _atlas = gfx.create_texture(_atlas_w, _atlas_h,
                                 gfx::PixelFormat::R8,
                                 gfx::FilterMode::Nearest,
                                 _atlas_bitmap.data());

    // Dynamic VBO: 4096 glyphs × 6 verts × 4 floats.
    _vbo = gfx.create_buffer(nullptr,
                              4096 * 6 * 4 * sizeof(float), true);
}

bool Font::load(gfx::Backend& gfx, float size_pt, float dpi, float content_scale) {
    _content_scale = (content_scale > 0.f) ? content_scale : 1.f;
    float px_size  = size_pt * dpi * _content_scale / 72.f;

    for (int i = 0; FONT_PATHS[i]; ++i) {
        if (_bake(FONT_PATHS[i], px_size)) {
            std::cerr << "[bl_ui] Font loaded: " << FONT_PATHS[i]
                      << "  px=" << px_size << "\n";
            _setup_gfx(gfx);
            if (_sh) {
                _line_h /= _content_scale;
                return true;
            }
            // GPU setup failed — clean up and try next font.
            _atlas = 0;
            _sh    = 0;
            _vbo   = 0;
        }
    }
    std::cerr << "[bl_ui] Font::load — no usable TTF font found\n";
    return false;
}

// ---------------------------------------------------------------------------
// UTF-8 decode helper
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
// draw_text
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

    float cs       = _content_scale;
    float cx       = x * cs;
    float baseline = std::round(y * cs) + _ascent;

    for (std::size_t i = 0; i < text.size(); ) {
        unsigned cp = utf8_decode(text, i);
        const BakedChar* bc = _find_char(cp);
        if (!bc) continue;

        float qx0 = cx + bc->xoff,  qy0 = baseline + bc->yoff;
        float qx1 = cx + bc->xoff2, qy1 = baseline + bc->yoff2;
        float qs0 = float(bc->x0),  qt0 = float(bc->y0);
        float qs1 = float(bc->x1),  qt1 = float(bc->y1);

        verts.insert(verts.end(), {qx0,qy0,qs0,qt0,  qx1,qy0,qs1,qt0,  qx1,qy1,qs1,qt1});
        verts.insert(verts.end(), {qx0,qy0,qs0,qt0,  qx1,qy1,qs1,qt1,  qx0,qy1,qs0,qt1});

        cx += bc->xadvance;
    }
    if (verts.empty()) return;

    int vertex_count = static_cast<int>(verts.size() / 4);
    static constexpr int VBO_CAPACITY = 4096 * 6;  // matches _setup_gfx allocation
    if (_vbo_vertex_top + vertex_count > VBO_CAPACITY) _vbo_vertex_top = 0;
    size_t byte_offset = (size_t)_vbo_vertex_top * 4 * sizeof(float);
    _gfx->update_buffer(_vbo, verts.data(), verts.size() * sizeof(float), byte_offset);
    int first_vertex = _vbo_vertex_top;
    _vbo_vertex_top += vertex_count;

    // Orthographic projection from physical pixels to NDC.
    float pw = _vp_w * cs, ph = _vp_h * cs;
    float proj[16] = {
         2.f/pw,    0.f,    0.f, 0.f,
         0.f,   -2.f/ph,   0.f, 0.f,
         0.f,       0.f,  -1.f, 0.f,
        -1.f,       1.f,   0.f, 1.f,
    };

    _gfx->set_blend_alpha(true);
    _gfx->use_shader(_sh);
    _gfx->uniform_m4("uProj", proj);
    _gfx->uniform_4f("uColor", color.rf(), color.gf(), color.bf(), color.af());
    _gfx->uniform_2i("uAtlasSize", _atlas_w, _atlas_h);
    _gfx->bind_texture(0, _atlas, "uTex");
    _gfx->draw_triangles(_vbo, LAYOUT_TEXT, first_vertex, vertex_count);
}

float Font::measure_text(std::string_view text) const {
    float w = 0.f;
    for (std::size_t i = 0; i < text.size(); ) {
        unsigned cp = utf8_decode(text, i);
        const BakedChar* bc = _find_char(cp);
        if (bc) w += bc->xadvance;
    }
    return w / _content_scale;
}

} // namespace bl_ui
