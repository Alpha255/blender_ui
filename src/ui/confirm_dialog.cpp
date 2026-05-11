#include "confirm_dialog.h"
#include "../render/theme.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Button visual constants — matches Blender's wcol_regular in the dark theme.
// Source: release/datafiles/userdef/userdef_default_theme.c  wcol_regular
// ---------------------------------------------------------------------------
static constexpr RGBA BTN_FILL    {0x52, 0x52, 0x52, 0xFF};  // inner
static constexpr RGBA BTN_FILL_HV {0x6E, 0x6E, 0x6E, 0xFF};  // inner_sel (hover)
static constexpr RGBA BTN_OUTLINE {0x19, 0x19, 0x19, 0xFF};  // outline
static constexpr RGBA BTN_TEXT    {0xDD, 0xDD, 0xDD, 0xFF};  // text
static constexpr RGBA BTN_TEXT_HV {0xFF, 0xFF, 0xFF, 0xFF};  // text_sel

// Dim overlay drawn behind the dialog (Blender uses ~25% black).
static constexpr RGBA OVERLAY_COLOR{0x00, 0x00, 0x00, 0x40};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ConfirmDialog::ConfirmDialog(std::string         title,
                             std::vector<Button> buttons,
                             float               vp_w,
                             float               vp_h,
                             Roundbox*           rb,
                             Font*               font,
                             IconAtlas*          icons,
                             int                 icon_id)
    : _title(std::move(title))
    , _buttons(std::move(buttons))
    , _icon_id(icon_id)
    , _vp_w(vp_w), _vp_h(vp_h)
    , _rb(rb), _font(font), _icons(icons)
{
    _layout();
}

void ConfirmDialog::set_viewport(float w, float h) {
    _vp_w = w;
    _vp_h = h;
    _layout();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void ConfirmDialog::_layout() {
    using namespace Theme;

    const float scale     = UI_SCALE;
    const float pad_x     = ITEM_PAD_X;             // horizontal inset inside dialog
    const float pad_y     = std::roundf(4.f * scale); // top/bottom of each section
    const float sep_h     = 1.f;                    // separator line thickness
    const float btn_gap   = std::roundf(4.f * scale); // gap between buttons
    const float icon_sz   = (_icons && _icons->ready() && _icon_id != 0)
                            ? _icons->icon_size() : 0.f;
    const float icon_gap  = (icon_sz > 0.f) ? std::roundf(4.f * scale) : 0.f;

    // Title row width
    float title_content_w = icon_sz + icon_gap + _font->measure_text(_title);
    float title_row_w     = pad_x + title_content_w + pad_x;

    // Button widths (at least 80px × UI_SCALE each)
    _btn_rects.resize(_buttons.size());
    float min_btn_w = std::roundf(80.f * scale);
    float total_btn_w = pad_x;
    for (std::size_t i = 0; i < _buttons.size(); ++i) {
        float bw = pad_x + _font->measure_text(_buttons[i].label) + pad_x;
        bw = std::max(bw, min_btn_w);
        _btn_rects[i].w = bw;
        total_btn_w += bw;
        if (i + 1 < _buttons.size()) total_btn_w += btn_gap;
    }
    total_btn_w += pad_x;

    // Dialog width: fit both title and buttons, with a minimum of 200px.
    float min_dlg_w = std::roundf(200.f * scale);
    _dlg_w = std::max({title_row_w, total_btn_w, min_dlg_w});

    // Dialog height:
    //   pad_y + ITEM_HEIGHT (title) + pad_y + sep_h + pad_y + ITEM_HEIGHT (btns) + pad_y
    _dlg_h = pad_y + ITEM_HEIGHT + pad_y + sep_h + pad_y + ITEM_HEIGHT + pad_y;
    _dlg_h = std::roundf(_dlg_h);

    // Center in viewport
    _dlg_x = std::roundf((_vp_w - _dlg_w) * 0.5f);
    _dlg_y = std::roundf((_vp_h - _dlg_h) * 0.5f);

    // Button positions — right-aligned within the dialog (last button = rightmost)
    // Blender: cancel on left, confirm on right.
    float bx = _dlg_x + pad_x;
    for (std::size_t i = 0; i < _buttons.size(); ++i) {
        _btn_rects[i].x = bx;
        _btn_rects[i].y = _dlg_y + pad_y + ITEM_HEIGHT + pad_y + sep_h + pad_y;
        _btn_rects[i].h = ITEM_HEIGHT;
        bx += _btn_rects[i].w + btn_gap;
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void ConfirmDialog::draw() {
    if (_result != OPEN) return;

    using namespace Theme;

    const float scale   = UI_SCALE;
    const float pad_x   = ITEM_PAD_X;
    const float pad_y   = std::roundf(4.f * scale);
    const float icon_sz = (_icons && _icons->ready() && _icon_id != 0)
                          ? _icons->icon_size() : 0.f;
    const float icon_gap= (icon_sz > 0.f) ? std::roundf(4.f * scale) : 0.f;

    // 1. Full-screen dim overlay
    _rb->draw_rect_filled(0.f, 0.f, _vp_w, _vp_h, OVERLAY_COLOR);

    // 2. Dialog background + border
    _rb->draw_roundbox(_dlg_x, _dlg_y, _dlg_w, _dlg_h,
                       MENU_RADIUS,
                       MENU_BG,
                       MENU_OUTLINE, 1.f);

    // 3. Title row — icon + text, vertically centered in the row
    float title_y  = _dlg_y + pad_y;
    float title_cx = _dlg_x + pad_x;

    if (icon_sz > 0.f) {
        float iy = title_y + (ITEM_HEIGHT - icon_sz) * 0.5f;
        _icons->draw_icon(_icon_id, title_cx, iy, HEADER_TEXT);
        title_cx += icon_sz + icon_gap;
    }

    float ty = title_y + (ITEM_HEIGHT - _font->line_height()) * 0.5f;
    _font->draw_text(_title, title_cx, ty, HEADER_TEXT);

    // 4. Separator — 1px horizontal line
    float sep_y  = _dlg_y + pad_y + ITEM_HEIGHT + pad_y;
    _rb->draw_line_h(_dlg_x + 4.f, sep_y, _dlg_w - 8.f, SEP_COLOR);

    // 5. Buttons
    for (std::size_t i = 0; i < _buttons.size(); ++i) {
        const BtnRect& br = _btn_rects[i];
        bool hv = br.hovered;

        RGBA fill    = hv ? BTN_FILL_HV : BTN_FILL;
        RGBA textcol = hv ? BTN_TEXT_HV : BTN_TEXT;

        _rb->draw_roundbox(br.x, br.y, br.w, br.h,
                           MENU_RADIUS,
                           fill,
                           BTN_OUTLINE, 1.f);

        float tx = br.x + (br.w - _font->measure_text(_buttons[i].label)) * 0.5f;
        float tty = br.y + (br.h - _font->line_height()) * 0.5f;
        _font->draw_text(_buttons[i].label, tx, tty, textcol);
    }
}

// ---------------------------------------------------------------------------
// Mouse handling
// ---------------------------------------------------------------------------

bool ConfirmDialog::handle_mouse(float mx, float my, bool pressed, bool /*released*/) {
    if (_result != OPEN) return false;

    // Update hover state
    for (auto& br : _btn_rects) {
        br.hovered = (mx >= br.x && mx < br.x + br.w &&
                      my >= br.y && my < br.y + br.h);
    }

    if (pressed) {
        for (std::size_t i = 0; i < _btn_rects.size(); ++i) {
            if (_btn_rects[i].hovered) {
                _result = static_cast<int>(i);
                return true;
            }
        }
        // Click outside the dialog → cancel
        if (mx < _dlg_x || mx > _dlg_x + _dlg_w ||
            my < _dlg_y || my > _dlg_y + _dlg_h)
        {
            _result = -1;
        }
    }
    return true; // consume all input while dialog is open
}

bool ConfirmDialog::handle_key(int glfw_key, int /*mods*/) {
    if (_result != OPEN) return false;

    if (glfw_key == GLFW_KEY_ESCAPE) {
        _result = -1;
        return true;
    }
    if (glfw_key == GLFW_KEY_ENTER || glfw_key == GLFW_KEY_KP_ENTER) {
        // Enter activates the last button (confirm / rightmost)
        _result = static_cast<int>(_buttons.size()) - 1;
        return true;
    }
    return true; // consume all keys while dialog is open
}

} // namespace bl_ui
