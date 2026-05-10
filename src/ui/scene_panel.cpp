#include "scene_panel.h"
#include "../render/theme.h"
#include <algorithm>
#include <cmath>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Visual constants
// ---------------------------------------------------------------------------

static constexpr RGBA COL_PANEL_BG  { 26,  26,  26, 220};
static constexpr RGBA COL_PANEL_BDR { 48,  48,  48, 255};
static constexpr RGBA COL_HDR_BG    { 35,  35,  35, 255};
static constexpr RGBA COL_HDR_TEXT  {215, 215, 215, 255};
static constexpr RGBA COL_SEP       { 48,  48,  48, 255};
static constexpr RGBA COL_ITEM_TEXT {180, 180, 180, 255};
static constexpr RGBA COL_ITEM_THOV {255, 255, 255, 255};
static constexpr RGBA COL_ITEM_HOV  { 71, 114, 179, 255};
static constexpr RGBA COL_ITEM_SEL  { 48,  88, 148, 255};
static constexpr RGBA COL_ARROW     {110, 110, 110, 255};
static constexpr RGBA COL_EYE_VIS  {120, 120, 120, 255};
static constexpr RGBA COL_EYE_HID  { 55,  55,  55, 255};

// ---------------------------------------------------------------------------
// Type color — one distinct colour per node kind
// ---------------------------------------------------------------------------

RGBA ScenePanel::_type_color(NodeType t) {
    switch (t) {
        case NodeType::Scene:      return {239, 170,  89, 255};  // orange
        case NodeType::Collection: return {115, 148, 219, 255};  // blue
        case NodeType::Mesh:       return {165, 165, 165, 255};  // grey
        case NodeType::Camera:     return { 90, 200, 200, 255};  // cyan
        case NodeType::Light:      return {240, 215,  75, 255};  // yellow
        case NodeType::Empty:      return { 95,  95,  95, 255};  // dark grey
    }
    return {128, 128, 128, 255};
}

// ---------------------------------------------------------------------------
// Panel header strip height (the "Scene Collection" title bar)
// ---------------------------------------------------------------------------

float ScenePanel::_panel_header_h() {
    return std::roundf(Theme::ITEM_HEIGHT + 4.f * Theme::UI_SCALE);
}

// ---------------------------------------------------------------------------
// Construction / setup
// ---------------------------------------------------------------------------

ScenePanel::ScenePanel() {
    _build_scene();
}

void ScenePanel::set_dependencies(Roundbox* rb, Font* font) {
    _rb   = rb;
    _font = font;
}

void ScenePanel::set_viewport(float w, float h) {
    _vp_w = w;
    _vp_h = h;
}

// Pre-populate a default scene hierarchy matching Blender's startup file.
void ScenePanel::_build_scene() {
    auto make = [](const char* name, NodeType t, bool exp = true) {
        auto n = std::make_unique<SceneNode>();
        n->name = name; n->type = t; n->expanded = exp;
        return n;
    };

    _root = make("Scene", NodeType::Scene);

    auto col = make("Scene Collection", NodeType::Collection);
    col->children.push_back(make("Camera",    NodeType::Camera));
    col->children.push_back(make("Light",     NodeType::Light));
    col->children.push_back(make("Cube",      NodeType::Mesh));

    // A sub-collection collapsed by default to show expand/collapse behaviour
    auto subcol = make("Objects", NodeType::Collection, false);
    subcol->children.push_back(make("Sphere",   NodeType::Mesh));
    subcol->children.push_back(make("Torus",    NodeType::Mesh));
    subcol->children.push_back(make("Armature", NodeType::Empty));
    col->children.push_back(std::move(subcol));

    _root->children.push_back(std::move(col));
}

// ---------------------------------------------------------------------------
// Flat list construction (depth-first, respects expanded state)
// ---------------------------------------------------------------------------

void ScenePanel::_flatten(SceneNode* node, int depth, float& cy) {
    float ih = std::roundf(Theme::ITEM_HEIGHT);
    _flat.push_back({node, depth, cy});
    cy += ih;
    if (node->expanded) {
        for (auto& child : node->children)
            _flatten(child.get(), depth + 1, cy);
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void ScenePanel::draw(float header_h) {
    if (!_rb) return;
    using namespace Theme;

    // Panel geometry
    _pw = std::roundf(200.f * UI_SCALE);
    _px = _vp_w - _pw;
    _py = header_h;
    _ph = _vp_h - header_h;

    float phdr = _panel_header_h();
    float ih   = std::roundf(ITEM_HEIGHT);

    // Rebuild flat item list
    _flat.clear();
    float cy = _py + phdr;
    _flatten(_root.get(), 0, cy);
    _content_h = cy - (_py + phdr);

    // Clamp scroll
    float max_scroll = std::max(0.f, _content_h - (_ph - phdr));
    _scroll = std::clamp(_scroll, 0.f, max_scroll);

    // --- Panel background ---
    _rb->draw_roundbox(_px, _py, _pw, _ph,
                       MENU_RADIUS, COL_PANEL_BG, COL_PANEL_BDR, 1.f);

    // --- Header strip ---
    _rb->draw_roundbox(_px, _py, _pw, phdr,
                       MENU_RADIUS, COL_HDR_BG, RGBA{0,0,0,0}, 0.f);

    if (_font) {
        float th = _font->line_height();
        float tx = _px + std::roundf(8.f * UI_SCALE);
        float ty = _py + (phdr - th) * 0.5f;
        _font->draw_text("Scene Collection", tx, ty, COL_HDR_TEXT);
    }

    // Separator under header
    _rb->draw_line_h(_px, _py + phdr, _pw, COL_SEP);

    // --- Item rows ---
    float clip_top    = _py + phdr;
    float clip_bottom = _py + _ph;

    for (int i = 0; i < static_cast<int>(_flat.size()); ++i) {
        float iy = _flat[i].y0 - _scroll;
        if (iy + ih <= clip_top)    continue;
        if (iy       >= clip_bottom) break;
        _draw_item(i, _hov_item == i, _sel_item == i);
    }
}

void ScenePanel::_draw_item(int idx, bool hovered, bool selected) {
    using namespace Theme;

    const FlatItem& fi = _flat[idx];
    float ih     = std::roundf(ITEM_HEIGHT);
    float indent = std::roundf(14.f * UI_SCALE);
    float iy     = fi.y0 - _scroll;

    // Row background highlight
    if (selected || hovered) {
        RGBA bg = selected ? COL_ITEM_SEL : COL_ITEM_HOV;
        _rb->draw_rect_filled(_px + 1.f, iy, _pw - 2.f, ih, bg);
    }

    RGBA tcol = (hovered || selected) ? COL_ITEM_THOV : COL_ITEM_TEXT;

    float lw  = std::max(1.f, 1.2f * UI_SCALE);
    float x   = _px + std::roundf(4.f * UI_SCALE) + fi.depth * indent;

    // --- Collapse / expand arrow ---
    bool has_ch = !fi.node->children.empty();
    float asz   = std::roundf(6.f * UI_SCALE);
    float acx   = x + asz * 0.5f;
    float acy   = iy + ih * 0.5f;

    if (has_ch) {
        if (fi.node->expanded) {
            // ▼ downward V — two line segments
            _rb->draw_line_segment(acx - asz * 0.55f, acy - asz * 0.35f,
                                   acx,               acy + asz * 0.4f,
                                   lw, COL_ARROW);
            _rb->draw_line_segment(acx,               acy + asz * 0.4f,
                                   acx + asz * 0.55f, acy - asz * 0.35f,
                                   lw, COL_ARROW);
        } else {
            // ▶ right-pointing filled triangle
            _rb->draw_triangle_right(acx - asz * 0.35f, acy - asz * 0.5f,
                                     asz * 0.65f, asz, COL_ARROW);
        }
    }

    x += std::roundf(13.f * UI_SCALE);

    // --- Type icon (small coloured rounded square) ---
    float isz  = std::roundf(7.f * UI_SCALE);
    float ir   = std::roundf(2.f * UI_SCALE);
    float ix   = x;
    float iy2  = iy + (ih - isz) * 0.5f;
    RGBA  icol = _type_color(fi.node->type);
    // Dim the icon when the node is hidden
    if (!fi.node->visible) icol = RGBA{60, 60, 60, 255};
    _rb->draw_roundbox(ix, iy2, isz, isz, ir, icol, RGBA{0,0,0,0}, 0.f);

    x += isz + std::roundf(5.f * UI_SCALE);

    // --- Label ---
    if (_font) {
        float th = _font->line_height();
        float ty = iy + (ih - th) * 0.5f;
        RGBA  tc = fi.node->visible ? tcol : COL_EYE_HID;
        _font->draw_text(fi.node->name, x, ty, tc);
    }

    // --- Eye (visibility toggle), right-aligned ---
    float esz = std::roundf(8.f * UI_SCALE);
    float ecx = _px + _pw - std::roundf(11.f * UI_SCALE);
    float ecy = iy + ih * 0.5f;
    RGBA  ecol = fi.node->visible ? COL_EYE_VIS : COL_EYE_HID;

    // Outer ellipse outline (eye shape)
    float ew = esz * 1.35f, eh = esz * 0.80f;
    _rb->draw_roundbox(ecx - ew * 0.5f, ecy - eh * 0.5f, ew, eh,
                       eh * 0.5f, RGBA{0,0,0,0}, ecol, 1.f);

    // Pupil dot (only when visible)
    if (fi.node->visible) {
        float pr = std::roundf(2.f * UI_SCALE);
        _rb->draw_roundbox(ecx - pr, ecy - pr, pr * 2.f, pr * 2.f,
                           pr, ecol, RGBA{0,0,0,0}, 0.f);
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool ScenePanel::handle_mouse(float mx, float my, bool pressed, bool /*released*/) {
    bool over = (mx >= _px && mx < _px + _pw &&
                 my >= _py && my < _py + _ph);

    _hov_item = -1;
    if (!over) return false;

    float ih = std::roundf(Theme::ITEM_HEIGHT);

    for (int i = 0; i < static_cast<int>(_flat.size()); ++i) {
        float iy = _flat[i].y0 - _scroll;
        if (my >= iy && my < iy + ih) {
            _hov_item = i;
            break;
        }
    }

    if (pressed && _hov_item >= 0) {
        FlatItem& fi  = _flat[_hov_item];
        float indent  = std::roundf(14.f * Theme::UI_SCALE);
        float arrow_x = _px + std::roundf(4.f * Theme::UI_SCALE) + fi.depth * indent;
        float arrow_end = arrow_x + std::roundf(13.f * Theme::UI_SCALE);
        float iy      = fi.y0 - _scroll;
        float ih_     = std::roundf(Theme::ITEM_HEIGHT);

        if (mx < arrow_end && !fi.node->children.empty()) {
            // Toggle expand / collapse
            fi.node->expanded = !fi.node->expanded;
        } else {
            // Check eye icon hit
            float esz = std::roundf(8.f * Theme::UI_SCALE);
            float ecx = _px + _pw - std::roundf(11.f * Theme::UI_SCALE);
            float ecy = iy + ih_ * 0.5f;
            if (mx >= ecx - esz && mx <= ecx + esz &&
                my >= ecy - esz && my <= ecy + esz) {
                fi.node->visible = !fi.node->visible;
            } else {
                _sel_item = _hov_item;
            }
        }
    }

    return over;
}

void ScenePanel::handle_scroll(float dy) {
    // dy from GLFW: positive = wheel forward (scroll up → move list down → increase _scroll)
    float ih = std::roundf(Theme::ITEM_HEIGHT);
    _scroll -= dy * ih * 2.f;
    _scroll = std::max(0.f, _scroll);
}

} // namespace bl_ui
