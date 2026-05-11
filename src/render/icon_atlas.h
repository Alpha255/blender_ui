#pragma once
#include <bl_ui/icons.h>
#include "theme.h"
#include "gfx/backend.h"
#include "gfx/vk_backend.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace bl_ui {

// ---------------------------------------------------------------------------
// IconAtlas — loads SVG icons on demand, caches rasterised pixels in a
// single GPU texture atlas.  All draw coordinates are logical (window) pixels.
// ---------------------------------------------------------------------------

class IconAtlas {
public:
    IconAtlas() = default;
    ~IconAtlas() = default;

    // svg_dir       : directory containing *.svg files.
    // icon_px       : logical icon size in pixels (default 16).
    // content_scale : HiDPI scale factor from glfwGetWindowContentScale.
    bool init(gfx::Backend& gfx, const std::string& svg_dir,
              float icon_px       = 16.f,
              float content_scale = 1.f);

    // Draw one icon at logical position (x, y), top-left origin.
    void draw_icon(int icon_id, float x, float y, RGBA tint);

    bool  ready()     const { return _tex != 0; }
    float icon_size() const { return _icon_px; }

    // Must be called once per frame before any draw_icon calls.
    void begin_frame() { _vbo_vertex_top = 0; }

    void set_viewport(float w, float h) { _vp_w = w; _vp_h = h; }

private:
    struct Cell {
        float u0, v0, u1, v1;
        bool  loaded = false;
    };

    bool _load_icon(int icon_id);
    bool _rasterise(int icon_id, const std::string& svg_path, int slot);
    static const char* _svg_stem(int icon_id);

    gfx::Backend*      _gfx = nullptr;
    std::string _svg_dir;
    float _icon_px       = 16.f;
    float _content_scale = 1.f;
    int   _phys_px       = 16;

    // 16×16 atlas grid → 256 slots max
    static constexpr int COLS      = 16;
    static constexpr int MAX_ICONS = COLS * COLS;

    int _atlas_w = 0, _atlas_h = 0;
    std::vector<unsigned char> _atlas_px;  // RGBA8, full atlas CPU copy

    gfx::TextureHandle _tex = 0;
    gfx::BufferHandle  _vbo = 0;   // dynamic: up to VBO_CAPACITY icon quads per frame
    gfx::ShaderHandle  _sh  = 0;
    int _vbo_vertex_top = 0;       // suballocation cursor (vertices), reset each frame

    static constexpr int VBO_CAPACITY = 256 * 6;  // max icons per frame × 6 verts

    std::unordered_map<int, Cell> _cells;
    int _next_slot = 0;

    float _vp_w = 800.f, _vp_h = 600.f;
};

} // namespace bl_ui
