#include "checkbox.h"
#include "../render/theme.h"
#include <cmath>
#include <algorithm>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Visual constants — mirror Blender wcol_option dark-theme values.
// Source: release/datafiles/userdef/userdef_default_theme.c
// ---------------------------------------------------------------------------

// Box fill colors
static constexpr RGBA COL_BOX_NORMAL   { 43,  43,  43, 255};  // unchecked inner
static constexpr RGBA COL_BOX_CHECKED  { 72, 114, 179, 255};  // checked inner (#4872B3)
static constexpr RGBA COL_BOX_HOVER    { 60,  60,  60, 255};  // unchecked inner on hover row
static constexpr RGBA COL_OUTLINE      { 23,  23,  23, 255};  // box border
static constexpr RGBA COL_OUTLINE_HOV  {100, 100, 100, 255};  // border when row is hovered

// Checkmark color (white, same as Blender's wcol_option.item when selected)
static constexpr RGBA COL_CHECK        {255, 255, 255, 255};

// Text
static constexpr RGBA COL_TEXT        {230, 230, 230, 255};
static constexpr RGBA COL_TEXT_HOV    {255, 255, 255, 255};

// Layout
static constexpr float BOX_FRACTION   = 0.65f;  // box size = BOX_FRACTION * ITEM_HEIGHT
static constexpr float LABEL_GAP      = 6.f;    // gap between box and label text (unscaled)
static constexpr float BOX_RADIUS     = 2.f;    // corner radius (unscaled)
static constexpr float OUTLINE_W      = 1.f;    // outline thickness px

// ---------------------------------------------------------------------------

void Checkbox::set_dependencies(Roundbox* rb, Font* font) {
    _rb   = rb;
    _font = font;
}

void Checkbox::set_viewport(float w, float h) {
    _vp_w = w;
    _vp_h = h;
}

// ---------------------------------------------------------------------------
// Layout — fills _rects parallel to _items.
// ---------------------------------------------------------------------------

void Checkbox::_build_rects(float x, float y) {
    _rects.resize(_items.size());

    float ih  = std::roundf(Theme::ITEM_HEIGHT);
    float bsz = std::roundf(BOX_FRACTION * ih);          // box side
    float gap = std::roundf(LABEL_GAP * Theme::UI_SCALE);

    float cy = y;
    for (int i = 0; i < (int)_items.size(); i++) {
        ItemRect& r = _rects[i];
        r.x = x;
        r.y = cy;
        r.w = bsz + gap + _font->measure_text(_items[i].label);
        r.h = ih;

        // Box centered vertically in the row
        r.bx = x;
        r.by = cy + std::roundf((ih - bsz) * 0.5f);
        r.bw = bsz;
        r.bh = bsz;

        cy += ih;
    }
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void Checkbox::draw(float x, float y, float mx, float my) {
    if (!_rb || !_font) return;

    _ox = x;
    _oy = y;

    _build_rects(x, y);

    // Detect hovered item
    _hov = -1;
    for (int i = 0; i < (int)_rects.size(); i++) {
        const ItemRect& r = _rects[i];
        if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
            _hov = i;
            break;
        }
    }

    float rad    = std::roundf(BOX_RADIUS * Theme::UI_SCALE);
    float outw   = OUTLINE_W * Theme::UI_SCALE;
    float gap    = std::roundf(LABEL_GAP * Theme::UI_SCALE);

    for (int i = 0; i < (int)_items.size(); i++) {
        const Item&     item = _items[i];
        const ItemRect& r    = _rects[i];
        bool hov = (i == _hov);

        // --- Checkbox box ---
        RGBA fill    = item.checked ? COL_BOX_CHECKED
                     : hov         ? COL_BOX_HOVER
                                   : COL_BOX_NORMAL;
        RGBA outline = hov ? COL_OUTLINE_HOV : COL_OUTLINE;

        _rb->draw_roundbox(r.bx, r.by, r.bw, r.bh, rad, fill, outline, outw);

        // --- Checkmark (drawn when checked) ---
        if (item.checked) {
            _rb->draw_checkmark(r.bx, r.by, r.bw, r.bh, COL_CHECK);
        }

        // --- Label ---
        float lx = r.bx + r.bw + gap;
        float ly = r.y + std::roundf(r.h * 0.5f - Theme::ITEM_HEIGHT * 0.35f);
        RGBA  tc = hov ? COL_TEXT_HOV : COL_TEXT;
        _font->draw_text(item.label, lx, ly, tc);
    }
}

// ---------------------------------------------------------------------------
// handle_mouse
// ---------------------------------------------------------------------------

bool Checkbox::handle_mouse(float mx, float my, bool pressed) {
    if (!pressed) return false;

    _build_rects(_ox, _oy);

    for (int i = 0; i < (int)_rects.size(); i++) {
        const ItemRect& r = _rects[i];
        if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
            _items[i].checked = !_items[i].checked;
            return true;
        }
    }
    return false;
}

} // namespace bl_ui
