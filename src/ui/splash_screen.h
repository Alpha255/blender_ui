#pragma once
#include "../render/roundbox.h"
#include "../render/font.h"
#include "../render/icon_atlas.h"
#include <string>

namespace bl_ui {

// ---------------------------------------------------------------------------
// SplashScreen — Blender-style startup splash dialog.
//
// Layout (top → bottom):
//   ┌──────────────────────────────┐
//   │  Artwork area  (~55% height) │  procedural dark bg + colored shapes
//   ├──────────────────────────────┤
//   │  Version strip               │  "blender_ui  0.1.0  2026-05-10"
//   ├───────────────┬──────────────┤
//   │  Quick Start  │ Recent Files │  two-column list
//   │  New File     │ (no recent)  │
//   │  Open File... │              │
//   │  Recover      │              │
//   └───────────────┴──────────────┘
//
// Closes on:  click outside dialog | Escape | any Quick Start button click
// ---------------------------------------------------------------------------

class SplashScreen {
public:
    SplashScreen(float vp_w, float vp_h,
                 Roundbox* rb, Font* font, IconAtlas* icons = nullptr);

    void set_viewport(float w, float h);
    void draw();

    bool handle_mouse(float mx, float my, bool pressed, bool released);
    bool handle_key(int glfw_key, int mods);

    bool is_closed()       const { return _closed; }
    bool wants_open_file() const { return _wants_open_file; }
    void clear_wants_open_file()  { _wants_open_file = false; }

private:
    void _layout();
    void _draw_artwork();
    void _draw_version_strip();
    void _draw_content();
    void _draw_footer();

    float _vp_w, _vp_h;

    // Dialog geometry
    float _dlg_x = 0.f, _dlg_y = 0.f;
    float _dlg_w = 0.f, _dlg_h = 0.f;

    // Section heights
    float _art_h    = 0.f;   // artwork
    float _strip_h  = 0.f;   // version strip
    float _cont_h   = 0.f;   // two-column content
    float _footer_h = 0.f;

    // Quick-start button rects (x,y,w,h) — stored for hit-testing
    struct BtnRect { float x, y, w, h; int id; };
    // id: 0=NewFile, 1=OpenFile, 2=Recover
    BtnRect _btns[3] = {};
    int     _hov_btn = -1;

    bool _closed          = false;
    bool _wants_open_file = false;

    Roundbox*  _rb;
    Font*      _font;
    IconAtlas* _icons;
};

} // namespace bl_ui
