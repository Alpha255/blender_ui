#pragma once

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

// Menu background  (wcol_menu_back)
constexpr RGBA MENU_BG      {0x18, 0x18, 0x18, 0xFF};
constexpr RGBA MENU_OUTLINE {0x24, 0x24, 0x24, 0xFF};
constexpr float MENU_RADIUS  = 4.f;     // 0.2 * UI_UNIT_Y (20px)

// Menu item        (wcol_menu_item)
// Source: userdef_default_theme.c → wcol_menu_item
constexpr RGBA ITEM_BG        {0x18, 0x18, 0x18, 0x00}; // inner       transparent
constexpr RGBA ITEM_HOVER     {0x3D, 0x3D, 0x3D, 0xFF}; // inner_sel   #3d3d3d
constexpr RGBA ITEM_TEXT      {0xE6, 0xE6, 0xE6, 0xFF}; // text        #e6e6e6
constexpr RGBA ITEM_TEXT_SEL  {0xFF, 0xFF, 0xFF, 0xFF}; // text_sel    #FFFFFF
constexpr RGBA ITEM_TEXT_DIM  {0x99, 0x99, 0x99, 0xFF}; // wcol_menu_back.text  #999999
constexpr RGBA ITEM_SECONDARY {0xFF, 0xFF, 0xFF, 0x8F}; // item        #FFFFFF @ 56% — submenu arrow, shortcut hints
constexpr float ITEM_HEIGHT    = 20.f;   // UI_UNIT_Y at 1x
constexpr float ITEM_PAD_X     = 8.f;   // left/right text padding

// Header / topbar bar
// Source: userdef_default_theme.c → space_topbar + wcol_pulldown
constexpr RGBA HEADER_BG    {0x18, 0x18, 0x18, 0xB3}; // space_topbar.header  #181818 @ 70%
constexpr RGBA HEADER_TEXT  {0xD9, 0xD9, 0xD9, 0xFF}; // wcol_pulldown.text   #D9D9D9
constexpr RGBA HEADER_HOVER {0xFF, 0xFF, 0xFF, 0x1A}; // wcol_pulldown.inner_sel  white @ 10%
constexpr float HEADER_H     = 26.f;  // px
constexpr float HEADER_PAD_X = 10.f;  // px

// Separator line
constexpr RGBA SEP_COLOR    {0x3D, 0x3D, 0x3D, 0xFF}; // #3D3D3D
constexpr float SEP_HEIGHT   = 7.f;   // px (total band height)

// Viewport / background
constexpr RGBA VIEWPORT_BG  {0x39, 0x39, 0x39, 0xFF}; // #393939

// Font
constexpr float FONT_SIZE_PT = 11.f;

// Submenu arrow right padding
constexpr float ARROW_PAD_R = 6.f;

} // namespace Theme
} // namespace bl_ui
