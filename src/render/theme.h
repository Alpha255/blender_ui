#pragma once
#include <cmath>
#include <algorithm>

namespace bl_ui {

// ---------------------------------------------------------------------------
// RGBA color value (0-255 components)
// ---------------------------------------------------------------------------

struct RGBA {
    unsigned char r, g, b, a;

    // Normalized [0,1] accessors for OpenGL uniforms
    float rf() const { return r / 255.f; }
    float gf() const { return g / 255.f; }
    float bf() const { return b / 255.f; }
    float af() const { return a / 255.f; }

    constexpr RGBA() : r(0), g(0), b(0), a(0) {}
    constexpr RGBA(unsigned char r, unsigned char g,
                   unsigned char b, unsigned char a = 255)
        : r(r), g(g), b(b), a(a) {}
};

// ---------------------------------------------------------------------------
// Dark theme constants (exact Blender values)
// Source: release/datafiles/userdef/userdef_default_theme.c
// ---------------------------------------------------------------------------

namespace Theme {

// ---------------------------------------------------------------------------
// Colors — always constexpr (no DPI dependence)
// ---------------------------------------------------------------------------

// Menu background  (wcol_menu_back)
constexpr RGBA MENU_BG      {0x18, 0x18, 0x18, 0xFF};
constexpr RGBA MENU_OUTLINE {0x24, 0x24, 0x24, 0xFF};

// Menu item  (wcol_menu_item)
constexpr RGBA ITEM_BG        {0x18, 0x18, 0x18, 0x00};
constexpr RGBA ITEM_HOVER     {0x3D, 0x3D, 0x3D, 0xFF};
constexpr RGBA ITEM_TEXT      {0xE6, 0xE6, 0xE6, 0xFF};
constexpr RGBA ITEM_TEXT_SEL  {0xFF, 0xFF, 0xFF, 0xFF};
constexpr RGBA ITEM_TEXT_DIM  {0x99, 0x99, 0x99, 0xFF};
constexpr RGBA ITEM_SECONDARY {0xFF, 0xFF, 0xFF, 0x8F};

// Header / topbar  (wcol_pulldown)
constexpr RGBA HEADER_BG    {0x18, 0x18, 0x18, 0xB3};
constexpr RGBA HEADER_TEXT  {0xD9, 0xD9, 0xD9, 0xFF};
constexpr RGBA HEADER_HOVER {0xFF, 0xFF, 0xFF, 0x1A};

// Separator
constexpr RGBA SEP_COLOR    {0x3D, 0x3D, 0x3D, 0xFF};

// Viewport / background
constexpr RGBA VIEWPORT_BG  {0x39, 0x39, 0x39, 0xFF};

// 2D viewport grid overlay
// Minor lines are slightly lighter than VIEWPORT_BG.
// Major lines (every 5 minor) are more prominent.
// Axis lines highlight world y=0 (horizontal) and world x=0 (vertical).
constexpr RGBA GRID_LINE       {0x45, 0x45, 0x45, 0xFF};
constexpr RGBA GRID_LINE_MAJOR {0x55, 0x55, 0x55, 0xFF};
constexpr RGBA GRID_AXIS_X     {0x7A, 0x24, 0x24, 0xFF}; // world y=0 horizontal line
constexpr RGBA GRID_AXIS_Y     {0x24, 0x5E, 0x1A, 0xFF}; // world x=0 vertical line

// Font size (points — DPI scaling handled separately in Font::load)
constexpr float FONT_SIZE_PT = 13.f;

// ---------------------------------------------------------------------------
// Layout metrics — scale with content_scale (UI_SCALE).
// Base values are for 1× (100% Windows scaling).
// Call set_ui_scale(content_scale) once in App::App() and on DPI change.
// Mirrors Blender: widget_unit = round(18 * U.scale_factor) + 2
// ---------------------------------------------------------------------------

inline float UI_SCALE     = 1.0f;  // = U.scale_factor = content_scale

inline float MENU_RADIUS  = 4.f;   // 0.2 * widget_unit
inline float ITEM_HEIGHT  = 20.f;  // widget_unit (UI_UNIT_Y)
inline float ITEM_PAD_X   = 8.f;   // UI_TEXT_MARGIN_X * widget_unit = 0.4 * 20
inline float HEADER_H     = 26.f;  // ED_area_headersize = widget_unit + 6*scale
inline float HEADER_PAD_X = 8.f;   // same as ITEM_PAD_X
inline float SEP_HEIGHT   = 7.f;   // separator band height
inline float ARROW_PAD_R  = 6.f;   // UI_MENU_SUBMENU_PADDING = 6 * UI_SCALE_FAC

// Mirrors WM_window_dpi_set_userdef():
//   auto_dpi = GetDpiForWindow() ≈ content_scale * 96
//   U.dpi    = auto_dpi * ui_scale * (72/96) = content_scale * 72
//   U.scale_factor = U.dpi / 72 = content_scale
//   pixelsize = max(1, U.dpi/64)
//   widget_unit = round(18 * scale_factor) + 2 * pixelsize
//   HEADER_H = widget_unit + HEADER_PADDING_Y(6) * scale_factor  (ED_area_headersize)
inline void set_ui_scale(float content_scale) {
    UI_SCALE         = content_scale;
    float dpi        = content_scale * 72.f;
    int   pixelsize  = std::max(1, (int)(dpi / 64.f));
    float wu         = std::roundf(18.f * content_scale) + 2.f * float(pixelsize);

    ITEM_HEIGHT  = wu;
    HEADER_H     = wu + 6.f * content_scale;   // ED_area_headersize()
    MENU_RADIUS  = 0.2f * wu;                   // wcol.roundness * widget_unit
    ITEM_PAD_X   = 0.4f * wu;                   // UI_TEXT_MARGIN_X * widget_unit
    HEADER_PAD_X = 0.4f * wu;
    SEP_HEIGHT   = std::roundf(0.35f * wu);     // ~7px at 1x
    ARROW_PAD_R  = 6.f  * content_scale;        // UI_MENU_SUBMENU_PADDING
}

} // namespace Theme
} // namespace bl_ui
