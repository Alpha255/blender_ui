#pragma once
#include <GL/glew.h>
#include "theme.h"
#include <string_view>

namespace bl_ui {

enum class TextAlign { LEFT, CENTER, RIGHT };

// ---------------------------------------------------------------------------
// Font — stb_truetype glyph atlas + OpenGL text renderer
// Covers printable ASCII (32-127) baked to a single 512x512 texture.
// ---------------------------------------------------------------------------

class Font {
public:
    Font() = default;
    ~Font();

    // Load a TTF file and bake glyph atlas.
    //   size_pt       — target size in logical points
    //   dpi           — base screen DPI (96 for Windows at 100% scaling)
    //   content_scale — HiDPI scale factor from glfwGetWindowContentScale
    //                   Atlas is baked at size_pt * dpi * content_scale / 72 px
    //                   so glyphs are crisp on HiDPI displays.
    // Returns false if no usable font file was found.
    bool load(float size_pt = 11.f, float dpi = 96.f, float content_scale = 1.f);

    // Draw text.  (x, y) is the top-left baseline position in screen px.
    void draw_text(std::string_view text, float x, float y,
                   RGBA color, TextAlign align = TextAlign::LEFT);

    // Measure pixel width of a string.
    float measure_text(std::string_view text) const;

    // Line height (ascent + descent + line gap).
    float line_height() const { return _line_h; }

    // Distance from the top of the bounding box to the text baseline, in logical px.
    float ascent() const { return _ascent / _content_scale; }

    bool ready() const { return _prog != 0 && _atlas != 0; }

    // Must be called whenever the logical (window) viewport size changes.
    void set_viewport(float w, float h) { _vp_w = w; _vp_h = h; }

private:
    bool _bake(const char* path, float px_size);
    void _setup_gl();

    GLuint _prog  = 0;
    GLuint _atlas = 0;
    GLuint _vao   = 0;
    GLuint _vbo   = 0;

    // Packed char data — binary-compatible with stbtt_packedchar.
    // All metric fields are in PHYSICAL pixels.
    struct BakedChar {
        unsigned short x0, y0, x1, y1; // atlas rect (physical px)
        float xoff,  yoff;              // cursor → quad top-left (physical px)
        float xadvance;                 // horizontal advance (physical px)
        float xoff2, yoff2;             // cursor → quad bottom-right (physical px)
    };
    BakedChar _chars[96]; // ASCII 32-127

    // Extra glyphs for Unicode chars needed by the UI (e.g. ▶ U+25B6)
    struct ExtraGlyph { unsigned codepoint; BakedChar bc; };
    static constexpr int EXTRA_MAX = 8;
    ExtraGlyph _extra[EXTRA_MAX] = {};
    int        _extra_count = 0;

    // Resolve a Unicode codepoint to a BakedChar pointer (nullptr = not available)
    const BakedChar* _find_char(unsigned cp) const;

    float _line_h         = 16.f;
    float _ascent         = 12.f;
    float _content_scale  = 1.f;  // divides physical glyph metrics → logical px
    float _vp_w           = 800.f;
    float _vp_h           = 600.f;
    int   _atlas_w        = 512;
    int   _atlas_h        = 512;

    // Uniform locations
    GLint _u_proj       = -1;
    GLint _u_tex        = -1;
    GLint _u_color      = -1;
    GLint _u_atlas_size = -1;  // ivec2 — atlas dimensions for manual bilinear bounds
};

} // namespace bl_ui
