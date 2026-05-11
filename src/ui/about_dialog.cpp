#include "about_dialog.h"
#include "../render/theme.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Visual constants
// ---------------------------------------------------------------------------

// Header — Blender brand orange (#E87D0D)
static constexpr RGBA ORANGE_HDR  {232, 125,  13, 255};
// Slightly darker orange for the bottom edge (fake gradient)
static constexpr RGBA ORANGE_DIM  {180,  97,  10, 255};
// Badge (dark rounded rect inside the orange header)
static constexpr RGBA BADGE_BG    { 22,  22,  22, 240};
// Button fill / hover (matches ConfirmDialog / file_dialog footer buttons)
static constexpr RGBA BTN_FILL    { 82,  82,  82, 255};  // #525252
static constexpr RGBA BTN_FILL_HV {110, 110, 110, 255};  // #6E6E6E
static constexpr RGBA BTN_OUTLINE { 25,  25,  25, 255};  // #191919

// Application metadata shown in the dialog
static const char* APP_NAME    = "blender_ui";
static const char* APP_TAGLINE = "Blender-Style Viewport & UI Library";
static const char* APP_VERSION = "0.1.0";
static const char* BUILD_DATE  = "2026-05-10";
static const char* BUILD_HASH  = "\xe2\x80\x94 (development build)";  // —
static const char* BUILD_BRANCH= "main";
// © U+00A9 encoded as UTF-8: 0xC2 0xA9
static const char* COPYRIGHT   = "\xc2\xa9 2024\xe2\x80\x93" "2026  blender_ui contributors";
static const char* LICENSE     = "Licensed under the GNU General Public License v3";
static const char* BASED_ON    = "Based on the Blender open-source project \xe2\x80\x94 blender.org";

// ---------------------------------------------------------------------------
// Constructor / layout
// ---------------------------------------------------------------------------

AboutDialog::AboutDialog(float vp_w, float vp_h,
                         Roundbox* rb, Font* font, IconAtlas* icons)
    : _vp_w(vp_w), _vp_h(vp_h), _rb(rb), _font(font), _icons(icons)
{
    _layout();
}

void AboutDialog::set_viewport(float w, float h) {
    _vp_w = w;
    _vp_h = h;
    _layout();
}

void AboutDialog::_layout() {
    using namespace Theme;

    const float pad   = std::roundf(12.f * UI_SCALE);
    const float row   = ITEM_HEIGHT;

    _header_h = std::roundf(80.f * UI_SCALE);

    // Info section: 4 rows (Version / Date / Hash / Branch) + top/bottom pads
    float info_h = pad + row * 4.f + pad;
    // Separator gap
    float sep_h  = std::roundf(10.f * UI_SCALE);
    // Copyright section: 3 text lines + top/bottom pads
    float copy_h = pad + row * 3.f + pad;

    _footer_h = std::roundf(8.f * UI_SCALE) + row + std::roundf(8.f * UI_SCALE);

    _dlg_w = std::roundf(440.f * UI_SCALE);
    _dlg_h = std::roundf(_header_h + info_h + sep_h + copy_h + _footer_h);
    _dlg_x = std::roundf((_vp_w - _dlg_w) * 0.5f);
    _dlg_y = std::roundf((_vp_h - _dlg_h) * 0.5f);

    // Close button (bottom-right of footer)
    const float hpad = std::roundf(ITEM_PAD_X * 1.5f);
    _btn_w = std::max(hpad + _font->measure_text("Close") + hpad,
                      std::roundf(80.f * UI_SCALE));
    _btn_h = row;
    _btn_x = _dlg_x + _dlg_w - hpad - _btn_w;
    _btn_y = _dlg_y + _dlg_h - _footer_h + (_footer_h - _btn_h) * 0.5f;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void AboutDialog::draw() {
    if (_closed) return;

    // Full-screen dim overlay
    _rb->draw_rect_filled(0.f, 0.f, _vp_w, _vp_h, RGBA{0, 0, 0, 0x50});

    // Drop shadow
    _rb->draw_rect_filled(_dlg_x + 3.f, _dlg_y + 4.f,
                          _dlg_w, _dlg_h, RGBA{0, 0, 0, 0x55});

    // Dialog outline (1px border via slightly larger rect)
    _rb->draw_rect_filled(_dlg_x - 1.f, _dlg_y - 1.f,
                          _dlg_w + 2.f, _dlg_h + 2.f, Theme::MENU_OUTLINE);

    // Dialog background
    _rb->draw_rect_filled(_dlg_x, _dlg_y, _dlg_w, _dlg_h, Theme::MENU_BG);

    _draw_header();
    _draw_body();
    _draw_footer();
}

void AboutDialog::_draw_header() {
    using namespace Theme;

    // Orange header background (split into two rects for a subtle gradient)
    float half = _header_h * 0.5f;
    _rb->draw_rect_filled(_dlg_x, _dlg_y,        _dlg_w, half,
                          RGBA{220, 115, 10, 255});        // top — slightly darker
    _rb->draw_rect_filled(_dlg_x, _dlg_y + half,  _dlg_w, _header_h - half,
                          ORANGE_HDR);                      // bottom — brand orange

    // Bottom edge separator
    _rb->draw_line_h(_dlg_x, _dlg_y + _header_h - 1.f, _dlg_w,
                     RGBA{0, 0, 0, 100});

    const float pad     = std::roundf(14.f * UI_SCALE);
    const float badge_s = std::roundf(48.f * UI_SCALE);
    const float badge_x = _dlg_x + pad;
    const float badge_y = _dlg_y + (_header_h - badge_s) * 0.5f;

    // Badge: dark rounded rect with orange outline
    _rb->draw_roundbox(badge_x, badge_y, badge_s, badge_s,
                       std::roundf(MENU_RADIUS * 1.5f),
                       BADGE_BG,
                       RGBA{255, 255, 255, 80}, 1.f);

    // "B" glyph centered in the badge
    const char* badge_lbl = "B";
    float bw = _font->measure_text(badge_lbl);
    float bh = _font->line_height();
    _font->draw_text(badge_lbl,
                     std::roundf(badge_x + (badge_s - bw) * 0.5f),
                     std::roundf(badge_y + (badge_s - bh) * 0.5f),
                     RGBA{255, 255, 255, 255});

    // Title and tagline to the right of the badge
    float tx  = badge_x + badge_s + std::roundf(12.f * UI_SCALE);
    float row = ITEM_HEIGHT;

    // Title — "blender_ui" (white, bold-feel: rendered twice offset by 1px)
    float title_y = badge_y + (badge_s * 0.35f - row * 0.5f);
    _font->draw_text(APP_NAME, tx + 1.f, title_y + 1.f,
                     RGBA{0, 0, 0, 100});   // shadow
    _font->draw_text(APP_NAME, tx, title_y, RGBA{255, 255, 255, 255});

    // Tagline — slightly dimmer white
    float sub_y = title_y + row + std::roundf(3.f * UI_SCALE);
    _font->draw_text(APP_TAGLINE, tx, sub_y, RGBA{255, 255, 255, 180});
}

void AboutDialog::_draw_body() {
    using namespace Theme;

    const float pad    = std::roundf(12.f * UI_SCALE);
    const float row    = ITEM_HEIGHT;
    const float hpad   = std::roundf(ITEM_PAD_X * 1.5f);
    const float lbl_w  = std::roundf(80.f * UI_SCALE);  // label column width

    float y = _dlg_y + _header_h + pad;

    // ---- Info rows (Version / Date / Hash / Branch) ------------------------
    const struct { const char* label; const char* value; } INFO[] = {
        {"Version:",  APP_VERSION},
        {"Date:",     BUILD_DATE},
        {"Hash:",     BUILD_HASH},
        {"Branch:",   BUILD_BRANCH},
    };

    for (auto& row_data : INFO) {
        float ty = y + (row - _font->line_height()) * 0.5f;
        // Right-aligned label
        float lw = _font->measure_text(row_data.label);
        _font->draw_text(row_data.label,
                         _dlg_x + hpad + lbl_w - lw, ty, ITEM_TEXT_DIM);
        // Left-aligned value
        _font->draw_text(row_data.value,
                         _dlg_x + hpad + lbl_w + std::roundf(8.f * UI_SCALE),
                         ty, HEADER_TEXT);
        y += row;
    }

    y += pad;   // bottom padding of info section

    // ---- Separator --------------------------------------------------------
    _rb->draw_line_h(_dlg_x + hpad, y, _dlg_w - hpad * 2.f, MENU_OUTLINE);
    y += std::roundf(10.f * UI_SCALE);

    // ---- Copyright / license text -----------------------------------------
    const char* COPY_LINES[] = { COPYRIGHT, LICENSE, BASED_ON };
    for (auto* line : COPY_LINES) {
        float ty = y + (row - _font->line_height()) * 0.5f;
        _font->draw_text(line, _dlg_x + hpad, ty, ITEM_TEXT_DIM);
        y += row;
    }
}

void AboutDialog::_draw_footer() {
    // Thin top separator line
    _rb->draw_line_h(_dlg_x,
                     _dlg_y + _dlg_h - _footer_h,
                     _dlg_w, Theme::MENU_OUTLINE);

    // Close button
    RGBA fill = _btn_hov ? BTN_FILL_HV : BTN_FILL;
    _rb->draw_roundbox(_btn_x, _btn_y, _btn_w, _btn_h,
                       Theme::MENU_RADIUS, fill, BTN_OUTLINE, 1.f);

    float tw = _font->measure_text("Close");
    float th = _font->line_height();
    _font->draw_text("Close",
                     std::roundf(_btn_x + (_btn_w - tw) * 0.5f),
                     std::roundf(_btn_y + (_btn_h - th) * 0.5f),
                     Theme::HEADER_TEXT);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool AboutDialog::handle_mouse(float mx, float my, bool pressed, bool released) {
    if (_closed) return false;

    // Update Close button hover
    _btn_hov = (mx >= _btn_x && mx < _btn_x + _btn_w &&
                my >= _btn_y && my < _btn_y + _btn_h);

    if (pressed && _btn_hov) {
        _closed = true;
    }
    return true;  // consume all events while open
}

bool AboutDialog::handle_key(int key, int /*mods*/) {
    if (_closed) return false;
    if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_ENTER ||
        key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_SPACE)
    {
        _closed = true;
        return true;
    }
    return true;
}

} // namespace bl_ui
