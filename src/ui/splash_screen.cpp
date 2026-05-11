#include "splash_screen.h"
#include "../render/theme.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Visual constants
// ---------------------------------------------------------------------------

static constexpr RGBA SPLASH_BG      { 28,  28,  28, 255};
static constexpr RGBA STRIP_BG       { 20,  20,  20, 255};
static constexpr RGBA CONT_BG        { 32,  32,  32, 255};
static constexpr RGBA FOOTER_BG      { 24,  24,  24, 255};
static constexpr RGBA SECTION_HDR    {180, 180, 180, 255};

static constexpr RGBA BTN_FILL       { 52,  52,  52, 255};
static constexpr RGBA BTN_FILL_HV    { 72,  72,  72, 255};
static constexpr RGBA BTN_OUTLINE    { 18,  18,  18, 255};

// Artwork decoration colors
static constexpr RGBA ART_ORANGE     {232, 125,  13, 200};
static constexpr RGBA ART_BLUE       { 40, 144, 255, 160};
static constexpr RGBA ART_GREEN      {139, 220,   0, 140};
static constexpr RGBA ART_RED        {220,  50,  70, 160};
static constexpr RGBA ART_DOT        {255, 255, 255,  40};

static const char* APP_VERSION = "0.1.0";
static const char* BUILD_DATE  = "2026-05-10";
// © U+00A9 (UTF-8: 0xC2 0xA9), — U+2014 (UTF-8: 0xE2 0x80 0x94)
static const char* FOOTER_TEXT = "\xc2\xa9 2024\xe2\x80\x93" "2026  blender_ui contributors  \xe2\x80\x94  GNU GPL v3";

// ---------------------------------------------------------------------------
// Constructor / layout
// ---------------------------------------------------------------------------

SplashScreen::SplashScreen(float vp_w, float vp_h,
                            Roundbox* rb, Font* font, IconAtlas* icons)
    : _vp_w(vp_w), _vp_h(vp_h), _rb(rb), _font(font), _icons(icons)
{
    _layout();
}

void SplashScreen::set_viewport(float w, float h) {
    _vp_w = w;
    _vp_h = h;
    _layout();
}

void SplashScreen::_layout() {
    using namespace Theme;

    // Blender formula: width ≈ style->widget.points * 45 * UI_SCALE_FAC
    // At 13pt font, widget ≈ 16px; 16*45 = 720px.
    _dlg_w = std::roundf(std::min(720.f * UI_SCALE, _vp_w - 40.f * UI_SCALE));
    _dlg_w = std::max(_dlg_w, 400.f * UI_SCALE);

    _strip_h  = std::roundf(ITEM_HEIGHT + 10.f * UI_SCALE);
    _footer_h = std::roundf(ITEM_HEIGHT + 8.f  * UI_SCALE);

    // Artwork: 55% of total dialog height (Blender: roughly the image half)
    // Content: 5 rows (section header + 3 buttons + gap)
    const float row = ITEM_HEIGHT;
    _cont_h = std::roundf(row * 5.f + 12.f * UI_SCALE);
    _art_h  = std::roundf(_cont_h * 1.1f);   // artwork slightly taller than content

    _dlg_h = std::roundf(_art_h + _strip_h + _cont_h + _footer_h);

    // Center in viewport
    _dlg_x = std::roundf((_vp_w - _dlg_w) * 0.5f);
    _dlg_y = std::roundf((_vp_h - _dlg_h) * 0.5f);

    // ---- Quick-start button layout ----------------------------------------
    const float pad  = std::roundf(12.f * UI_SCALE);
    const float col_w = std::roundf(_dlg_w * 0.5f);  // left column width
    const float bw   = col_w - pad * 2.f;

    float cont_y = _dlg_y + _art_h + _strip_h;
    float by = cont_y + row + std::roundf(6.f * UI_SCALE);  // skip section header row

    const char* labels[] = { "New File", "Open File...", "Recover Last Session" };
    for (int i = 0; i < 3; ++i) {
        _btns[i] = { _dlg_x + pad, by, bw, row, i };
        by += row + std::roundf(2.f * UI_SCALE);
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void SplashScreen::draw() {
    if (_closed) return;

    // Dim overlay
    _rb->draw_rect_filled(0.f, 0.f, _vp_w, _vp_h, RGBA{0, 0, 0, 0x60});

    // Drop shadow
    _rb->draw_rect_filled(_dlg_x + 4.f, _dlg_y + 6.f,
                          _dlg_w, _dlg_h, RGBA{0, 0, 0, 0x70});

    // Border
    _rb->draw_rect_filled(_dlg_x - 1.f, _dlg_y - 1.f,
                          _dlg_w + 2.f, _dlg_h + 2.f, Theme::MENU_OUTLINE);

    // Base background
    _rb->draw_rect_filled(_dlg_x, _dlg_y, _dlg_w, _dlg_h, SPLASH_BG);

    _draw_artwork();
    _draw_version_strip();
    _draw_content();
    _draw_footer();
}

// Procedural artwork: dark background with a few colored circles/ellipses
// and scattered dot stars, imitating Blender's release artwork style.
void SplashScreen::_draw_artwork() {
    const float ax = _dlg_x;
    const float ay = _dlg_y;
    const float aw = _dlg_w;
    const float ah = _art_h;

    // Dark base (already drawn as SPLASH_BG, just add a slightly different shade)
    _rb->draw_rect_filled(ax, ay, aw, ah, RGBA{22, 22, 22, 255});

    // Large background circle — orange glow, bottom-left
    {
        float r = ah * 0.80f;
        float cx = ax + aw * 0.12f;
        float cy = ay + ah * 0.75f;
        // Simulate glow with several increasingly transparent circles
        _rb->draw_roundbox(cx - r, cy - r, r * 2.f, r * 2.f, r,
                           RGBA{140, 70, 8, 30}, RGBA{0,0,0,0}, 0.f);
        r *= 0.65f;
        _rb->draw_roundbox(cx - r, cy - r, r * 2.f, r * 2.f, r,
                           RGBA{200, 100, 12, 40}, RGBA{0,0,0,0}, 0.f);
        r *= 0.55f;
        _rb->draw_roundbox(cx - r, cy - r, r * 2.f, r * 2.f, r,
                           ART_ORANGE, RGBA{0,0,0,0}, 0.f);
    }

    // Blue circle — upper right
    {
        float r = ah * 0.55f;
        float cx = ax + aw * 0.85f;
        float cy = ay + ah * 0.25f;
        _rb->draw_roundbox(cx - r, cy - r, r * 2.f, r * 2.f, r,
                           RGBA{20, 72, 160, 35}, RGBA{0,0,0,0}, 0.f);
        r *= 0.60f;
        _rb->draw_roundbox(cx - r, cy - r, r * 2.f, r * 2.f, r,
                           RGBA{30, 100, 220, 50}, RGBA{0,0,0,0}, 0.f);
        r *= 0.50f;
        _rb->draw_roundbox(cx - r, cy - r, r * 2.f, r * 2.f, r,
                           ART_BLUE, RGBA{0,0,0,0}, 0.f);
    }

    // Green accent circle — center-right
    {
        float r = ah * 0.30f;
        float cx = ax + aw * 0.65f;
        float cy = ay + ah * 0.60f;
        _rb->draw_roundbox(cx - r, cy - r, r * 2.f, r * 2.f, r,
                           RGBA{70, 120, 0, 40}, RGBA{0,0,0,0}, 0.f);
        r *= 0.55f;
        _rb->draw_roundbox(cx - r, cy - r, r * 2.f, r * 2.f, r,
                           ART_GREEN, RGBA{0,0,0,0}, 0.f);
    }

    // Red accent — upper left area
    {
        float r = ah * 0.22f;
        float cx = ax + aw * 0.32f;
        float cy = ay + ah * 0.22f;
        _rb->draw_roundbox(cx - r, cy - r, r * 2.f, r * 2.f, r,
                           RGBA{160, 30, 50, 50}, RGBA{0,0,0,0}, 0.f);
        r *= 0.55f;
        _rb->draw_roundbox(cx - r, cy - r, r * 2.f, r * 2.f, r,
                           ART_RED, RGBA{0,0,0,0}, 0.f);
    }

    // Scattered dot stars (deterministic pattern using fixed offsets)
    static const float DOT_POS[][2] = {
        {0.10f, 0.15f}, {0.20f, 0.40f}, {0.30f, 0.08f}, {0.45f, 0.30f},
        {0.55f, 0.12f}, {0.62f, 0.78f}, {0.70f, 0.45f}, {0.78f, 0.68f},
        {0.88f, 0.10f}, {0.92f, 0.55f}, {0.15f, 0.85f}, {0.40f, 0.90f},
        {0.50f, 0.55f}, {0.25f, 0.60f}, {0.82f, 0.38f}, {0.95f, 0.82f},
        {0.08f, 0.50f}, {0.72f, 0.22f}, {0.35f, 0.72f}, {0.60f, 0.92f},
    };
    for (auto& dp : DOT_POS) {
        float dx = ax + dp[0] * aw;
        float dy = ay + dp[1] * ah;
        float dr = 1.5f * Theme::UI_SCALE;
        _rb->draw_roundbox(dx - dr, dy - dr, dr * 2.f, dr * 2.f, dr,
                           ART_DOT, RGBA{0,0,0,0}, 0.f);
    }

    // "blender_ui" watermark text — centered, large, low-alpha
    {
        const char* wm = "blender_ui";
        float tw = _font->measure_text(wm);
        float th = _font->line_height();
        _font->draw_text(wm,
                         std::roundf(ax + (aw - tw) * 0.5f),
                         std::roundf(ay + (ah - th) * 0.5f),
                         RGBA{255, 255, 255, 18});
    }

    // Bottom separator between artwork and version strip
    _rb->draw_line_h(ax, ay + ah - 1.f, aw, RGBA{0, 0, 0, 120});
}

void SplashScreen::_draw_version_strip() {
    using namespace Theme;

    const float sx = _dlg_x;
    const float sy = _dlg_y + _art_h;
    const float sw = _dlg_w;

    _rb->draw_rect_filled(sx, sy, sw, _strip_h, STRIP_BG);

    // "blender_ui" bold label (shadow trick)
    const float pad = std::roundf(14.f * UI_SCALE);
    float ty = sy + (_strip_h - _font->line_height()) * 0.5f;

    _font->draw_text("blender_ui", sx + pad + 1.f, ty + 1.f, RGBA{0,0,0,120});
    _font->draw_text("blender_ui", sx + pad, ty, ITEM_TEXT);

    // Version / date — right-aligned
    char ver_buf[64];
    std::snprintf(ver_buf, sizeof(ver_buf), "%s  %s", APP_VERSION, BUILD_DATE);
    float vw = _font->measure_text(ver_buf);
    _font->draw_text(ver_buf, sx + sw - pad - vw, ty, ITEM_TEXT_DIM);

    _rb->draw_line_h(sx, sy + _strip_h - 1.f, sw, MENU_OUTLINE);
}

void SplashScreen::_draw_content() {
    using namespace Theme;

    const float cx  = _dlg_x;
    const float cy  = _dlg_y + _art_h + _strip_h;
    const float cw  = _dlg_w;
    const float col = std::roundf(cw * 0.5f);  // column split
    const float pad = std::roundf(12.f * UI_SCALE);
    const float row = ITEM_HEIGHT;

    _rb->draw_rect_filled(cx, cy, cw, _cont_h, CONT_BG);

    // Vertical column divider
    _rb->draw_line_h(cx + col, cy, 1.f, MENU_OUTLINE);  // 1-wide vertical via line_h trick
    // Actually draw_line_h is horizontal; use rect for vertical:
    _rb->draw_rect_filled(cx + col, cy, 1.f, _cont_h, MENU_OUTLINE);

    // ---- Left column: Quick Start ----
    float lx = cx + pad;
    float ly = cy + std::roundf(6.f * UI_SCALE);

    // Section header
    _font->draw_text("Quick Start", lx, ly + (row - _font->line_height()) * 0.5f,
                     SECTION_HDR);
    ly += row + std::roundf(2.f * UI_SCALE);

    const char* btn_labels[] = { "New File", "Open File...", "Recover Last Session" };
    for (int i = 0; i < 3; ++i) {
        BtnRect& b = _btns[i];
        RGBA fill = (_hov_btn == i) ? BTN_FILL_HV : BTN_FILL;
        _rb->draw_roundbox(b.x, b.y, b.w, b.h, MENU_RADIUS, fill, BTN_OUTLINE, 1.f);

        float tw = _font->measure_text(btn_labels[i]);
        float th = _font->line_height();
        _font->draw_text(btn_labels[i],
                         std::roundf(b.x + (b.w - tw) * 0.5f),
                         std::roundf(b.y + (b.h - th) * 0.5f),
                         HEADER_TEXT);
    }

    // ---- Right column: Recent Files ----
    float rx = cx + col + pad;
    float ry = cy + std::roundf(6.f * UI_SCALE);

    _font->draw_text("Recent Files", rx, ry + (row - _font->line_height()) * 0.5f,
                     SECTION_HDR);
    ry += row + std::roundf(8.f * UI_SCALE);

    _font->draw_text("(no recent files)", rx, ry + (row - _font->line_height()) * 0.5f,
                     ITEM_TEXT_DIM);

    // Top separator
    _rb->draw_line_h(cx, cy, cw, MENU_OUTLINE);
}

void SplashScreen::_draw_footer() {
    using namespace Theme;

    const float fx = _dlg_x;
    const float fy = _dlg_y + _art_h + _strip_h + _cont_h;

    _rb->draw_rect_filled(fx, fy, _dlg_w, _footer_h, FOOTER_BG);
    _rb->draw_line_h(fx, fy, _dlg_w, MENU_OUTLINE);

    float tw = _font->measure_text(FOOTER_TEXT);
    float ty = fy + (_footer_h - _font->line_height()) * 0.5f;
    _font->draw_text(FOOTER_TEXT,
                     std::roundf(fx + (_dlg_w - tw) * 0.5f),
                     std::roundf(ty),
                     ITEM_TEXT_DIM);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool SplashScreen::handle_mouse(float mx, float my, bool pressed, bool /*released*/) {
    if (_closed) return false;

    // Update button hover
    _hov_btn = -1;
    for (int i = 0; i < 3; ++i) {
        const BtnRect& b = _btns[i];
        if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
            _hov_btn = i;
            break;
        }
    }

    if (pressed) {
        // Click outside dialog → close
        bool inside = (mx >= _dlg_x && mx < _dlg_x + _dlg_w &&
                       my >= _dlg_y && my < _dlg_y + _dlg_h);
        if (!inside) {
            _closed = true;
            return true;
        }

        if (_hov_btn == 1) {         // "Open File..."
            _wants_open_file = true;
            _closed = true;
        } else if (_hov_btn >= 0) {  // "New File" or "Recover"
            _closed = true;
        }
        // Click inside but not on a button: keep open (except artwork area closes it)
        float art_bottom = _dlg_y + _art_h;
        if (my < art_bottom) {
            _closed = true;   // click on artwork closes splash (Blender behavior)
        }
    }

    return true;  // consume all events while open
}

bool SplashScreen::handle_key(int key, int /*mods*/) {
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
