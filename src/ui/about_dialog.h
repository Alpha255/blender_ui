#pragma once
#include "../render/roundbox.h"
#include "../render/font.h"
#include "../render/icon_atlas.h"

namespace bl_ui {

// ---------------------------------------------------------------------------
// AboutDialog — "About blender_ui" dialog, triggered by Help › About.
//
// Matches Blender's wm.splash_about / WM_OT_splash_about visual style:
//   • Orange header strip with app name, tagline, and logo badge.
//   • Info section: Version / Date / Hash / Branch in two-column layout.
//   • Separator + copyright and license lines.
//   • Footer: single [Close] button (right-aligned).
//
// Reference: source/blender/editors/space_info/info_ops.cc
//            scripts/startup/bl_ui/space_info.py  (TOPBAR_MT_help)
// ---------------------------------------------------------------------------

class AboutDialog {
public:
    AboutDialog(float vp_w, float vp_h,
                Roundbox* rb, Font* font, IconAtlas* icons = nullptr);

    void set_viewport(float w, float h);
    void draw();

    bool handle_mouse(float mx, float my, bool pressed, bool released);
    bool handle_key(int glfw_key, int mods);

    bool is_closed() const { return _closed; }

private:
    void _layout();
    void _draw_header();
    void _draw_body();
    void _draw_footer();

    float _vp_w, _vp_h;

    // Dialog geometry (recomputed on resize)
    float _dlg_x = 0.f, _dlg_y = 0.f;
    float _dlg_w = 0.f, _dlg_h = 0.f;
    float _header_h = 0.f;
    float _footer_h = 0.f;

    // Close button rect
    float _btn_x = 0.f, _btn_y = 0.f;
    float _btn_w = 0.f, _btn_h = 0.f;
    bool  _btn_hov = false;

    bool _closed = false;

    Roundbox*  _rb;
    Font*      _font;
    IconAtlas* _icons;
};

} // namespace bl_ui
