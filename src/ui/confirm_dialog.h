#pragma once
#include "../render/roundbox.h"
#include "../render/font.h"
#include "../render/icon_atlas.h"
#include <string>
#include <vector>

namespace bl_ui {

// ---------------------------------------------------------------------------
// ConfirmDialog — modal blocking dialog matching Blender's confirm popup.
//
// Renders a full-screen dim overlay + a centered dialog box with:
//   • Title row: icon (optional) + title text
//   • 1px separator
//   • A horizontal row of buttons
//
// Visual style mirrors wm_operator_confirm_message_ex / UI_popup_menu:
//   background  : MENU_BG (#181818), 1px MENU_OUTLINE border, MENU_RADIUS corners
//   buttons     : wcol_regular — #6E6E6E fill, #191919 outline, hover brightens
//   primary btn : drawn first on the right; no special pre-highlight
//
// Input:
//   Escape or clicking Cancel    → result() == -1 (cancelled)
//   Clicking a button            → result() == button index (0-based)
//   Enter                        → activates the last (rightmost) button
// ---------------------------------------------------------------------------

class ConfirmDialog {
public:
    struct Button {
        std::string label;
        bool        is_cancel = false;   // true → escape/Enter-cancel activates this
    };

    // title     — text shown in the header row (e.g. "Quit Blender?")
    // buttons   — left-to-right list; typically ["Cancel", "Quit Blender"]
    // icon_id   — BLIcon enum value to draw beside the title (0 = none)
    ConfirmDialog(std::string         title,
                  std::vector<Button> buttons,
                  float               vp_w,
                  float               vp_h,
                  Roundbox*           rb,
                  Font*               font,
                  IconAtlas*          icons   = nullptr,
                  int                 icon_id = 0);

    void set_viewport(float w, float h);

    void draw();

    // Returns true while the input was consumed.
    bool handle_mouse(float mx, float my, bool pressed, bool released);
    bool handle_key  (int glfw_key, int mods);

    bool is_closed() const { return _result != OPEN; }

    // -1 = cancelled (Escape or Cancel button)
    // 0, 1, … = button index clicked
    int  result()    const { return _result; }

private:
    static constexpr int OPEN = -2;

    void _layout();

    // Button geometry (screen-space)
    struct BtnRect {
        float x, y, w, h;
        bool  hovered = false;
    };

    std::string         _title;
    std::vector<Button> _buttons;
    int                 _icon_id;

    float _vp_w, _vp_h;
    float _dlg_x = 0.f, _dlg_y = 0.f;
    float _dlg_w = 0.f, _dlg_h = 0.f;

    std::vector<BtnRect> _btn_rects;

    int _result = OPEN;

    Roundbox*  _rb;
    Font*      _font;
    IconAtlas* _icons;
};

} // namespace bl_ui
