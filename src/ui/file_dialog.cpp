#include "file_dialog.h"
#include "../render/theme.h"
#include <bl_ui/icons.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Theme constants — match Blender's dark SpaceFile theme
// Source: release/datafiles/userdef/userdef_default_theme.c  (ThemeSpace)
// ---------------------------------------------------------------------------

// file list area background = TH_BACK
static constexpr RGBA FILE_BG          {0x39, 0x39, 0x39, 0xFF};
// every other row = TH_ROW_ALTERNATE
static constexpr RGBA FILE_BG_ALT      {0x36, 0x36, 0x36, 0xFF};
// selected row highlight = TH_HILITE / wcol_item inner_sel
static constexpr RGBA FILE_SEL         {0x47, 0x72, 0xB3, 0xFF};
// hover (non-selected) = TH_BACK + shade 35 as subtle overlay
static constexpr RGBA FILE_HOVER       {0xFF, 0xFF, 0xFF, 0x28};
// sidebar / left panel background
static constexpr RGBA SIDEBAR_BG       {0x2E, 0x2E, 0x2E, 0xFF};
// sidebar section divider / header
static constexpr RGBA SIDEBAR_HDR_BG   {0x26, 0x26, 0x26, 0xFF};
// top/bottom bar background
static constexpr RGBA BAR_BG           {0x26, 0x26, 0x26, 0xFF};
// text input field  (wcol_text inner)
static constexpr RGBA INPUT_BG         {0x3D, 0x3D, 0x3D, 0xFF};
static constexpr RGBA INPUT_OUTLINE    {0x19, 0x19, 0x19, 0xFF};
// nav button fill / hover
static constexpr RGBA NAV_FILL         {0x3A, 0x3A, 0x3A, 0xFF};
static constexpr RGBA NAV_FILL_HV      {0x50, 0x50, 0x50, 0xFF};
static constexpr RGBA NAV_OUTLINE      {0x19, 0x19, 0x19, 0xFF};
// scrollbar track + handle
static constexpr RGBA SB_TRACK        {0x2A, 0x2A, 0x2A, 0xFF};
static constexpr RGBA SB_HANDLE       {0x4A, 0x4A, 0x4A, 0xFF};
static constexpr RGBA SB_HANDLE_HV    {0x66, 0x66, 0x66, 0xFF};

// ---------------------------------------------------------------------------
// Constructor / setup
// ---------------------------------------------------------------------------

FileDialog::FileDialog(float vp_w, float vp_h,
                       Roundbox* rb, Font* font, IconAtlas* icons)
    : _vp_w(vp_w), _vp_h(vp_h)
    , _rb(rb), _font(font), _icons(icons)
{
    _compute_layout();
    _init_bookmarks();

    // Start in user home directory (fallback: current path)
    fs::path start;
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (home && fs::exists(home)) {
        start = fs::path(home);
    } else {
        start = fs::current_path();
    }
    _navigate_to(start, false);
}

void FileDialog::set_viewport(float w, float h) {
    _vp_w = w; _vp_h = h;
    _compute_layout();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void FileDialog::_compute_layout() {
    using namespace Theme;
    _ly.vp_w        = _vp_w;
    _ly.vp_h        = _vp_h;
    _ly.header_h    = HEADER_H;
    _ly.sidebar_w   = std::roundf(180.f * UI_SCALE);
    _ly.footer_h    = ITEM_HEIGHT * 2.f + ITEM_PAD_X * 2.f + std::roundf(4.f * UI_SCALE);
    _ly.scrollbar_w = std::roundf(8.f * UI_SCALE);

    // Dialog is a floating popup — ~80% of viewport, clamped to sensible bounds
    _ly.dlg_w = std::roundf(std::clamp(_vp_w * 0.80f, 520.f, 960.f));
    _ly.dlg_h = std::roundf(std::clamp(_vp_h * 0.78f, 380.f, 680.f));
    _ly.dlg_x = std::roundf((_vp_w - _ly.dlg_w) * 0.5f);
    _ly.dlg_y = std::roundf((_vp_h - _ly.dlg_h) * 0.5f);
}

// ---------------------------------------------------------------------------
// Bookmarks
// ---------------------------------------------------------------------------

void FileDialog::_init_bookmarks() {
    _bookmarks.clear();

    // System section
    _bookmarks.push_back({"System", {}, true});
#ifdef _WIN32
    for (char c = 'A'; c <= 'Z'; ++c) {
        std::string drv = std::string(1, c) + ":\\";
        if (fs::exists(fs::path(drv))) {
            _bookmarks.push_back({drv.substr(0, 2), fs::path(drv), false});
        }
    }
#else
    _bookmarks.push_back({"Root", fs::path("/"), false});
#endif

    // Bookmarks section
    _bookmarks.push_back({"Bookmarks", {}, true});
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (home) {
        fs::path h(home);
        _bookmarks.push_back({"Home",      h,                     false});
        _bookmarks.push_back({"Desktop",   h / "Desktop",         false});
        _bookmarks.push_back({"Documents", h / "Documents",       false});
        _bookmarks.push_back({"Downloads", h / "Downloads",       false});
        _bookmarks.push_back({"Pictures",  h / "Pictures",        false});
    }
}

// ---------------------------------------------------------------------------
// File system navigation
// ---------------------------------------------------------------------------

void FileDialog::_navigate_to(const fs::path& path, bool push) {
    if (!fs::is_directory(path)) return;

    _current  = path;
    _selected = -1;
    _hv_file  = -1;
    _scroll_y = 0.f;

    // Build entry list
    _entries.clear();
    try {
        for (auto& de : fs::directory_iterator(path,
                         fs::directory_options::skip_permission_denied))
        {
            // Skip hidden files (starting with '.')
            auto name = de.path().filename().string();
            if (!name.empty() && name[0] == '.') continue;

            Entry e;
            e.path   = de.path();
            e.name   = name;
            e.is_dir = de.is_directory();
            if (!e.is_dir) {
                try { e.size_str = _format_size(de.file_size()); }
                catch (...) {}
            }
            _entries.push_back(std::move(e));
        }
    } catch (const std::exception& ex) {
        std::cerr << "[FileDialog] " << ex.what() << "\n";
    }

    // Sort: directories first, then alphabetical (case-insensitive)
    std::sort(_entries.begin(), _entries.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        std::string na = a.name, nb = b.name;
        std::transform(na.begin(), na.end(), na.begin(), ::tolower);
        std::transform(nb.begin(), nb.end(), nb.begin(), ::tolower);
        return na < nb;
    });

    // Fill filename field with the directory name
    _filename = _current.filename().string();
    _cursor   = static_cast<int>(_filename.size());

    if (push) {
        // Truncate forward history
        if (_hist_idx + 1 < static_cast<int>(_history.size()))
            _history.erase(_history.begin() + _hist_idx + 1, _history.end());
        _history.push_back(_current);
        _hist_idx = static_cast<int>(_history.size()) - 1;
    }
}

void FileDialog::_go_parent() {
    fs::path parent = _current.parent_path();
    if (parent != _current)
        _navigate_to(parent);
}

void FileDialog::_go_back() {
    if (_hist_idx > 0) {
        --_hist_idx;
        _navigate_to(_history[_hist_idx], false);
    }
}

void FileDialog::_go_forward() {
    if (_hist_idx + 1 < static_cast<int>(_history.size())) {
        ++_hist_idx;
        _navigate_to(_history[_hist_idx], false);
    }
}

std::string FileDialog::_format_size(uintmax_t bytes) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    if (bytes < 1024)              ss << bytes << " B";
    else if (bytes < 1024 * 1024) ss << bytes / 1024.0 << " KiB";
    else if (bytes < (uintmax_t)1024 * 1024 * 1024)
                                   ss << bytes / (1024.0 * 1024.0) << " MiB";
    else                           ss << bytes / (1024.0 * 1024.0 * 1024.0) << " GiB";
    return ss.str();
}

// ---------------------------------------------------------------------------
// Draw — top level
// ---------------------------------------------------------------------------

void FileDialog::draw() {
    if (_closed) return;

    // Dim everything behind the dialog (25% black overlay)
    _rb->draw_rect_filled(0.f, 0.f, _vp_w, _vp_h, RGBA{0, 0, 0, 0x40});

    // Subtle drop-shadow (slightly offset dark rect)
    _rb->draw_rect_filled(_ly.dlg_x + 3.f, _ly.dlg_y + 4.f,
                          _ly.dlg_w, _ly.dlg_h, RGBA{0, 0, 0, 0x50});

    // Dialog border (1px outline via a slightly larger darker rect)
    _rb->draw_rect_filled(_ly.dlg_x - 1.f, _ly.dlg_y - 1.f,
                          _ly.dlg_w + 2.f, _ly.dlg_h + 2.f, Theme::MENU_OUTLINE);

    // Dialog background
    _rb->draw_rect_filled(_ly.dlg_x, _ly.dlg_y, _ly.dlg_w, _ly.dlg_h, FILE_BG);

    _draw_sidebar();
    _draw_file_list();
    _draw_scrollbar();
    _draw_header();   // drawn last so it renders on top of list top edge
    _draw_footer();
}

// ---------------------------------------------------------------------------
// Draw — HEADER (top bar)
// Mirrors: ED_region_header() + file_panel_path_back_draw_header()
// ---------------------------------------------------------------------------

void FileDialog::_draw_header() {
    using namespace Theme;
    const float h = _ly.header_h;

    _rb->draw_rect_filled(_ly.dlg_x, _ly.dlg_y, _ly.dlg_w, h, BAR_BG);
    // Bottom separator
    _rb->draw_line_h(_ly.dlg_x, _ly.dlg_y + h - 1.f, _ly.dlg_w, MENU_OUTLINE);

    const float btn_sz = ITEM_HEIGHT;
    const float y      = _ly.dlg_y + (h - btn_sz) * 0.5f;
    float x = _ly.dlg_x + ITEM_PAD_X;

    bool can_back = _hist_idx > 0;
    bool can_fwd  = _hist_idx + 1 < static_cast<int>(_history.size());
    bool can_up   = _current.has_parent_path() && _current.parent_path() != _current;

    _draw_nav_btn(x, y, btn_sz, "\xe2\x97\x84", can_back, _hv_nav == 0);  // ◄
    x += btn_sz + 2.f;
    _draw_nav_btn(x, y, btn_sz, "\xe2\x96\xba", can_fwd,  _hv_nav == 1);  // ►
    x += btn_sz + 2.f;
    _draw_nav_btn(x, y, btn_sz, "\xe2\x86\x91", can_up,   _hv_nav == 2);  // ↑
    x += btn_sz + 2.f;
    _draw_nav_btn(x, y, btn_sz, "\xe2\x86\xba", true,     _hv_nav == 3);  // ↺
    x += btn_sz + ITEM_PAD_X;

    // Path breadcrumb bar — editable text field with the current directory
    float path_w = _ly.dlg_x + _ly.dlg_w - x - ITEM_PAD_X;
    _rb->draw_roundbox(x, y, path_w, btn_sz,
                       MENU_RADIUS, INPUT_BG, INPUT_OUTLINE, 1.f);

    // Draw path components as " component > component > ..." (right-clipped)
    // Build list of components from root to leaf
    std::vector<std::string> parts;
    {
        fs::path p = _current;
        while (true) {
            std::string part = p.filename().string();
            if (part.empty()) {
                // root
                part = p.root_name().string();
                if (part.empty()) part = "/";
                parts.insert(parts.begin(), part);
                break;
            }
            parts.insert(parts.begin(), part);
            fs::path pp = p.parent_path();
            if (pp == p) break;
            p = pp;
        }
    }

    float tx0  = x + ITEM_PAD_X * 0.5f;
    float tx1  = x + path_w - ITEM_PAD_X * 0.5f;  // right clip edge
    float ty   = y + (btn_sz - _font->line_height()) * 0.5f;
    float cx   = tx0;

    for (std::size_t i = 0; i < parts.size(); ++i) {
        const char* sep = (i + 1 < parts.size()) ? " \xe2\x80\xba " : ""; // " › "
        float pw = _font->measure_text(parts[i]);
        float sw = _font->measure_text(sep);
        if (cx + pw + sw > tx1 && i + 1 < parts.size()) {
            // Doesn't fit — show "…" and the tail
            _font->draw_text("\xe2\x80\xa6", cx, ty, ITEM_TEXT_DIM);  // …
            break;
        }
        RGBA tc = (i + 1 == parts.size()) ? HEADER_TEXT : ITEM_TEXT_DIM;
        _font->draw_text(parts[i], cx, ty, tc);
        cx += pw;
        if (!parts.empty() && i + 1 < parts.size()) {
            _font->draw_text(sep, cx, ty, ITEM_TEXT_DIM);
            cx += sw;
        }
    }
}

// Single navigation button (◄ ► ↑ ↺)
void FileDialog::_draw_nav_btn(float x, float y, float sz,
                               const char* label, bool enabled, bool hovered)
{
    using namespace Theme;
    RGBA fill    = hovered ? NAV_FILL_HV : NAV_FILL;
    RGBA textcol = enabled ? HEADER_TEXT : ITEM_TEXT_DIM;
    _rb->draw_roundbox(x, y, sz, sz, MENU_RADIUS, fill, NAV_OUTLINE, 1.f);
    float tx = x + (sz - _font->measure_text(label)) * 0.5f;
    float ty = y + (sz - _font->line_height()) * 0.5f;
    _font->draw_text(label, tx, ty, textcol);
}

// ---------------------------------------------------------------------------
// Draw — SIDEBAR (left bookmarks panel)
// Mirrors: file_tools_region_draw() → ED_region_panels()
// ---------------------------------------------------------------------------

void FileDialog::_draw_sidebar() {
    using namespace Theme;
    float x = _ly.dlg_x;
    float y = _ly.dlg_y + _ly.header_h;
    float w = _ly.sidebar_w;
    float h = _ly.dlg_h - _ly.header_h;

    _rb->draw_rect_filled(x, y, w, h, SIDEBAR_BG);
    // Right separator
    _rb->draw_line_v(x + w - 1.f, y, h, MENU_OUTLINE);

    float cy = y + std::roundf(4.f * UI_SCALE);

    for (int i = 0; i < static_cast<int>(_bookmarks.size()); ++i) {
        const Bookmark& bm = _bookmarks[i];

        if (bm.is_header) {
            // Section header row  (like Blender's panel header strip)
            float rh = std::roundf(ITEM_HEIGHT * 0.8f);
            _rb->draw_rect_filled(x, cy, w, rh, SIDEBAR_HDR_BG);
            float ty = cy + (rh - _font->line_height()) * 0.5f;
            _font->draw_text(bm.label, x + ITEM_PAD_X, ty, ITEM_TEXT_DIM);
            cy += rh;
        } else {
            // Bookmark item
            bool hovered  = (i == _hv_bm);
            bool selected = (bm.path == _current);

            if (selected) {
                _rb->draw_roundbox(x + 2.f, cy, w - 4.f, ITEM_HEIGHT,
                                   MENU_RADIUS, FILE_SEL);
            } else if (hovered) {
                _rb->draw_roundbox(x + 2.f, cy, w - 4.f, ITEM_HEIGHT,
                                   MENU_RADIUS, FILE_HOVER);
            }

            RGBA tc = selected ? Theme::ITEM_TEXT_SEL : HEADER_TEXT;
            float ty = cy + (ITEM_HEIGHT - _font->line_height()) * 0.5f;

            // Icon (drive / folder / home etc.) — use folder icon if atlas ready
            float ix = x + ITEM_PAD_X;
            if (_icons && _icons->ready()) {
                float isz = _icons->icon_size();
                _icons->draw_icon(ICON_FILE_FOLDER, ix,
                                  cy + (ITEM_HEIGHT - isz) * 0.5f, tc);
                ix += isz + std::roundf(4.f * UI_SCALE);
            }
            _font->draw_text(bm.label, ix, ty, tc);

            cy += ITEM_HEIGHT;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw — FILE LIST (main region)
// Mirrors: file_draw_list() in file_draw.cc
// ---------------------------------------------------------------------------

void FileDialog::_draw_file_list() {
    using namespace Theme;
    const float lx = _ly.list_x();
    const float ly = _ly.list_y();
    const float lw = _ly.list_w();
    const float lh = _ly.list_h();

    // Background
    _rb->draw_rect_filled(lx, ly, lw, lh, FILE_BG);

    // Clip rows to list area (using scissor test)
    // glScissor uses bottom-left, physical framebuffer coords.
    // We'll skip scissor for simplicity and just check bounds per item.

    const float row_h   = ITEM_HEIGHT;
    const float icon_sz = (_icons && _icons->ready()) ? _icons->icon_size() : 0.f;
    const float icon_gap= (icon_sz > 0.f) ? std::roundf(4.f * UI_SCALE) : 0.f;
    const float size_col_w = _font->measure_text("999.9 MiB") + ITEM_PAD_X;

    int  visible_start = static_cast<int>(_scroll_y / row_h);
    int  visible_count = static_cast<int>(lh / row_h) + 2;

    for (int i = visible_start;
         i < static_cast<int>(_entries.size()) && i < visible_start + visible_count;
         ++i)
    {
        const Entry& e  = _entries[i];
        float iy        = ly + i * row_h - _scroll_y;

        if (iy + row_h < ly || iy > ly + lh) continue;  // outside clip

        bool selected = (i == _selected);
        bool hovered  = (i == _hv_file);

        // Alternating row background (TH_ROW_ALTERNATE) — even rows are slightly darker
        if ((i & 1) == 0) {
            _rb->draw_rect_filled(lx, iy, lw, row_h, FILE_BG_ALT);
        }

        // Selection / hover highlight
        if (selected) {
            _rb->draw_rect_filled(lx, iy, lw, row_h, FILE_SEL);
        } else if (hovered) {
            _rb->draw_rect_filled(lx, iy, lw, row_h, FILE_HOVER);
        }

        RGBA tc  = selected ? ITEM_TEXT_SEL : HEADER_TEXT;
        float ty = iy + (row_h - _font->line_height()) * 0.5f;
        float tx = lx + ITEM_PAD_X;

        // Icon
        if (_icons && _icons->ready()) {
            int icon_id = e.is_dir ? ICON_FILE_FOLDER : ICON_FILE_NEW;
            _icons->draw_icon(icon_id, tx, iy + (row_h - icon_sz) * 0.5f, tc);
            tx += icon_sz + icon_gap;
        } else {
            // Colored square fallback
            RGBA ic = e.is_dir
                ? RGBA{0xCC, 0x88, 0x22, 0xFF}
                : RGBA{0x99, 0x99, 0x99, 0xFF};
            _rb->draw_rect_filled(tx, iy + (row_h - 12.f) * 0.5f, 12.f, 12.f, ic);
            tx += 12.f + std::roundf(4.f * UI_SCALE);
        }

        // File name — clip so it doesn't overlap the size column
        float name_max_w = lx + lw - size_col_w - ITEM_PAD_X - tx;
        float name_w     = _font->measure_text(e.name);
        std::string display_name = e.name;
        if (name_w > name_max_w && !display_name.empty()) {
            // Simple middle-clip (Blender uses UI_text_clip_middle_ex)
            while (!display_name.empty() &&
                   _font->measure_text(display_name + "\xe2\x80\xa6") > name_max_w)
            {
                display_name.pop_back();
            }
            display_name += "\xe2\x80\xa6"; // …
        }
        _font->draw_text(display_name, tx, ty, tc);

        // File size — right-aligned, dimmer text
        if (!e.size_str.empty()) {
            float sw = _font->measure_text(e.size_str);
            RGBA  sc = selected
                ? RGBA{tc.r, tc.g, tc.b, (unsigned char)(tc.a * 0.7f)}
                : ITEM_TEXT_DIM;
            _font->draw_text(e.size_str,
                             lx + lw - ITEM_PAD_X - sw, ty, sc);
        }
    }

    // Column separator  — matches Blender's file column header dividers
    // Draw a faint line between name and size columns
    float sep_x = lx + lw - size_col_w;
    RGBA  sep_c {0x3F, 0x3F, 0x3F, 0xFF};
    _rb->draw_line_v(sep_x, ly, lh, sep_c);
}

// ---------------------------------------------------------------------------
// Draw — SCROLLBAR
// ---------------------------------------------------------------------------

void FileDialog::_draw_scrollbar() {
    using namespace Theme;
    const float total_h   = static_cast<float>(_entries.size()) * ITEM_HEIGHT;
    const float visible_h = _ly.list_h();
    if (total_h <= visible_h) return;

    const float tx = _ly.list_x() + _ly.list_w();
    const float ty = _ly.list_y();
    const float th = visible_h;
    const float tw = _ly.scrollbar_w;

    _rb->draw_rect_filled(tx, ty, tw, th, SB_TRACK);

    float max_scroll = total_h - visible_h;
    float handle_h   = std::max(std::roundf(30.f * UI_SCALE),
                                th * visible_h / total_h);
    float handle_y   = ty + (_scroll_y / max_scroll) * (th - handle_h);

    bool sb_hv = _sb_dragging;
    _rb->draw_roundbox(tx + 1.f, handle_y, tw - 2.f, handle_h,
                       std::roundf(MENU_RADIUS * 0.5f),
                       sb_hv ? SB_HANDLE_HV : SB_HANDLE);
}

// ---------------------------------------------------------------------------
// Draw — FOOTER (execute region)
// Mirrors: file_panel_operator_header_draw() + footer buttons
// ---------------------------------------------------------------------------

void FileDialog::_draw_footer() {
    using namespace Theme;
    const float fy = _ly.footer_y();
    const float fw = _ly.dlg_w;
    const float fh = _ly.footer_h;
    const float pad = ITEM_PAD_X;
    const float scale = UI_SCALE;

    // Footer background
    _rb->draw_rect_filled(_ly.dlg_x, fy, fw, fh, BAR_BG);
    _rb->draw_line_h(_ly.dlg_x, fy, fw, MENU_OUTLINE);

    const float row_pad = std::roundf(4.f * UI_SCALE);
    float y1 = fy + row_pad;  // top of first row (filename)

    // "File Name:" label
    const char* label = "File Name:";
    float lw = _font->measure_text(label);
    float lty = y1 + (ITEM_HEIGHT - _font->line_height()) * 0.5f;
    _font->draw_text(label, _ly.dlg_x + pad, lty, HEADER_TEXT);

    // Buttons: [Cancel] [Open]
    const char* btn_cancel_str = "Cancel";
    const char* btn_open_str   = "Open";
    float bcw = pad + _font->measure_text(btn_cancel_str) + pad;
    float bow  = pad + _font->measure_text(btn_open_str)  + pad;
    float btn_min_w = std::roundf(80.f * scale);
    bcw = std::max(bcw, btn_min_w);
    bow = std::max(bow, btn_min_w);

    // Buttons on the right of the SECOND row
    float y2   = y1 + ITEM_HEIGHT + row_pad;
    float bx2  = _ly.dlg_x + fw - pad - bow;
    float bx1  = bx2 - std::roundf(4.f * scale) - bcw;

    // Button backgrounds + text
    auto draw_btn = [&](float bx, float by, float bw, const char* str,
                        bool primary) {
        RGBA fill = primary
            ? RGBA{0x52, 0x52, 0x52, 0xFF}   // same as ConfirmDialog BTN_FILL
            : RGBA{0x3A, 0x3A, 0x3A, 0xFF};
        _rb->draw_roundbox(bx, by, bw, ITEM_HEIGHT,
                           MENU_RADIUS, fill, RGBA{0x19, 0x19, 0x19, 0xFF}, 1.f);
        float tx = bx + (bw - _font->measure_text(str)) * 0.5f;
        float ty = by + (ITEM_HEIGHT - _font->line_height()) * 0.5f;
        _font->draw_text(str, tx, ty, HEADER_TEXT);
    };

    draw_btn(bx1, y2, bcw, btn_cancel_str, false);
    draw_btn(bx2, y2, bow, btn_open_str,   true);

    // Filename input field — spans from after "File Name:" label to before buttons
    float input_x = _ly.dlg_x + pad + lw + std::roundf(6.f * scale);
    float input_w = bx1 - input_x - std::roundf(4.f * scale);
    _rb->draw_roundbox(input_x, y1, input_w, ITEM_HEIGHT,
                       MENU_RADIUS, INPUT_BG, INPUT_OUTLINE, 1.f);

    // Filename text + cursor
    float text_x = input_x + pad * 0.5f;
    float text_y = y1 + (ITEM_HEIGHT - _font->line_height()) * 0.5f;
    _font->draw_text(_filename, text_x, text_y, HEADER_TEXT);

    // Cursor bar (always shown — matches Blender's active text cursor)
    float before_cursor = _font->measure_text(
        std::string_view(_filename).substr(0, _cursor));
    float cur_x = text_x + before_cursor;
    _rb->draw_line_v(cur_x, y1 + 2.f, ITEM_HEIGHT - 4.f,
                     RGBA{0xFF, 0xFF, 0xFF, 0xFF});
}

// ---------------------------------------------------------------------------
// Hit-testing helpers
// ---------------------------------------------------------------------------

int FileDialog::_hit_file(float mx, float my) const {
    if (mx < _ly.list_x() || mx > _ly.list_x() + _ly.list_w()) return -1;
    if (my < _ly.list_y() || my > _ly.list_y() + _ly.list_h()) return -1;
    float row_h = Theme::ITEM_HEIGHT;
    int idx = static_cast<int>((my - _ly.list_y() + _scroll_y) / row_h);
    if (idx < 0 || idx >= static_cast<int>(_entries.size())) return -1;
    return idx;
}

int FileDialog::_hit_bookmark(float mx, float my) const {
    if (mx < _ly.dlg_x || mx > _ly.dlg_x + _ly.sidebar_w) return -1;
    if (my < _ly.dlg_y + _ly.header_h || my > _ly.dlg_y + _ly.dlg_h) return -1;
    float cy = _ly.dlg_y + _ly.header_h + std::roundf(4.f * Theme::UI_SCALE);
    for (int i = 0; i < static_cast<int>(_bookmarks.size()); ++i) {
        const Bookmark& bm = _bookmarks[i];
        float row_h = bm.is_header
            ? std::roundf(Theme::ITEM_HEIGHT * 0.8f)
            : Theme::ITEM_HEIGHT;
        if (my >= cy && my < cy + row_h) return i;
        cy += row_h;
    }
    return -1;
}

int FileDialog::_hit_nav(float mx, float my) const {
    using namespace Theme;
    float h      = _ly.header_h;
    float btn_sz = ITEM_HEIGHT;
    float y      = _ly.dlg_y + (h - btn_sz) * 0.5f;
    if (my < y || my > y + btn_sz) return -1;
    float x = _ly.dlg_x + ITEM_PAD_X;
    for (int i = 0; i < 4; ++i) {
        if (mx >= x && mx < x + btn_sz) return i;
        x += btn_sz + 2.f;
    }
    return -1;
}

// Footer hit-test helpers (use raw coordinates)
static bool in_rect(float mx, float my, float x, float y, float w, float h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

// ---------------------------------------------------------------------------
// Mouse handling
// ---------------------------------------------------------------------------

bool FileDialog::handle_mouse(float mx, float my, bool pressed, bool released) {
    using namespace Theme;

    // Update hovers
    _hv_file = _hit_file(mx, my);
    _hv_bm   = _hit_bookmark(mx, my);
    _hv_nav  = _hit_nav(mx, my);

    // Scrollbar drag
    if (_sb_dragging) {
        if (released) {
            _sb_dragging = false;
        } else {
            float total_h   = static_cast<float>(_entries.size()) * ITEM_HEIGHT;
            float visible_h = _ly.list_h();
            float max_scroll= total_h - visible_h;
            if (max_scroll > 0.f) {
                float dy      = my - _sb_drag_y0;
                float handle_h= std::max(std::roundf(30.f * UI_SCALE),
                                         _ly.list_h() * visible_h / total_h);
                float track_h = _ly.list_h() - handle_h;
                _scroll_y     = std::clamp(_sb_drag_sc0 + dy * max_scroll / track_h,
                                           0.f, max_scroll);
            }
        }
        return true;
    }

    if (!pressed) return true;

    // ---- Nav buttons -------------------------------------------------------
    int nav = _hit_nav(mx, my);
    if (nav >= 0) {
        switch (nav) {
            case 0: _go_back();    break;
            case 1: _go_forward(); break;
            case 2: _go_parent();  break;
            case 3: _navigate_to(_current, false); break;  // refresh
        }
        return true;
    }

    // ---- Scrollbar ---------------------------------------------------------
    {
        float tx = _ly.list_x() + _ly.list_w();
        float ty = _ly.list_y();
        if (in_rect(mx, my, tx, ty, _ly.scrollbar_w, _ly.list_h())) {
            _sb_dragging    = true;
            _sb_drag_y0     = my;
            _sb_drag_sc0    = _scroll_y;
            return true;
        }
    }

    // ---- Sidebar bookmarks -------------------------------------------------
    int bm = _hit_bookmark(mx, my);
    if (bm >= 0 && !_bookmarks[bm].is_header) {
        _navigate_to(_bookmarks[bm].path);
        return true;
    }

    // ---- File list ---------------------------------------------------------
    int fi = _hit_file(mx, my);
    if (fi >= 0) {
        double now     = glfwGetTime();
        bool dbl_click = (fi == _last_click_idx) &&
                         (now - _last_click_time < 0.35);
        _last_click_time = now;
        _last_click_idx  = fi;

        const Entry& e = _entries[fi];
        _selected = fi;

        if (dbl_click) {
            if (e.is_dir) {
                _navigate_to(e.path);
            } else {
                // Open file
                _result_path = e.path.string();
                _confirmed   = true;
                _closed      = true;
            }
        } else {
            // Single click: fill filename field
            _filename = e.name;
            _cursor   = static_cast<int>(_filename.size());
        }
        return true;
    }

    // ---- Footer buttons ----------------------------------------------------
    {
        float pad   = Theme::ITEM_PAD_X;
        float scale = Theme::UI_SCALE;
        float fy    = _ly.footer_y();
        float y1    = fy + std::roundf(4.f * scale);
        float y2    = y1 + ITEM_HEIGHT + std::roundf(4.f * scale);

        const char* btn_cancel_str = "Cancel";
        const char* btn_open_str   = "Open";
        float bcw = std::max(pad + _font->measure_text(btn_cancel_str) + pad,
                             std::roundf(80.f * scale));
        float bow  = std::max(pad + _font->measure_text(btn_open_str)  + pad,
                             std::roundf(80.f * scale));
        float bx2  = _ly.dlg_x + _ly.dlg_w - pad - bow;
        float bx1  = bx2 - std::roundf(4.f * scale) - bcw;

        if (in_rect(mx, my, bx1, y2, bcw, ITEM_HEIGHT)) {
            _closed = true;  // Cancel
            return true;
        }
        if (in_rect(mx, my, bx2, y2, bow, ITEM_HEIGHT)) {
            // Open — build full path from filename
            fs::path result = _current / _filename;
            _result_path = result.string();
            _confirmed   = true;
            _closed      = true;
            return true;
        }

        // Click in filename field — focus it (already focused by default)
        float lw    = _font->measure_text("File Name:");
        float input_x = _ly.dlg_x + pad + lw + std::roundf(6.f * scale);
        float input_w = bx1 - input_x - std::roundf(4.f * scale);
        if (in_rect(mx, my, input_x, y1, input_w, ITEM_HEIGHT)) {
            // Move cursor to click position (approximate by measuring)
            float rel    = mx - input_x - pad * 0.5f;
            int   new_cur = 0;
            for (int i = 0; i <= static_cast<int>(_filename.size()); ++i) {
                float w = _font->measure_text(
                    std::string_view(_filename).substr(0, i));
                if (w > rel) break;
                new_cur = i;
            }
            _cursor = new_cur;
            return true;
        }
    }

    // Click outside everything → close (like Blender's backdrop dismiss)
    // Actually Blender doesn't close on backdrop click in file browser —
    // only Cancel button or Escape closes it.

    return true;  // consume all events
}

bool FileDialog::handle_scroll(float delta_y) {
    if (_closed) return false;
    float total_h    = static_cast<float>(_entries.size()) * Theme::ITEM_HEIGHT;
    float visible_h  = _ly.list_h();
    float max_scroll = std::max(0.f, total_h - visible_h);
    _scroll_y = std::clamp(_scroll_y - delta_y * Theme::ITEM_HEIGHT * 3.f,
                            0.f, max_scroll);
    return true;
}

// ---------------------------------------------------------------------------
// Keyboard handling
// ---------------------------------------------------------------------------

bool FileDialog::handle_key(int key, int mods) {
    if (_closed) return false;
    (void)mods;

    switch (key) {
        case GLFW_KEY_ESCAPE:
            _closed = true;
            return true;

        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            if (_selected >= 0 && _entries[_selected].is_dir) {
                _navigate_to(_entries[_selected].path);
            } else {
                fs::path result = _current / _filename;
                _result_path = result.string();
                _confirmed   = true;
                _closed      = true;
            }
            return true;

        case GLFW_KEY_BACKSPACE:
            if (mods & GLFW_MOD_ALT) {
                _go_back();
            } else if (_cursor > 0) {
                _filename.erase(_cursor - 1, 1);
                --_cursor;
            }
            return true;

        case GLFW_KEY_DELETE:
            if (_cursor < static_cast<int>(_filename.size()))
                _filename.erase(_cursor, 1);
            return true;

        case GLFW_KEY_LEFT:
            if (_cursor > 0) --_cursor;
            return true;

        case GLFW_KEY_RIGHT:
            if (_cursor < static_cast<int>(_filename.size())) ++_cursor;
            return true;

        case GLFW_KEY_HOME:
            _cursor = 0;
            return true;

        case GLFW_KEY_END:
            _cursor = static_cast<int>(_filename.size());
            return true;

        case GLFW_KEY_UP:
            if (_selected > 0) {
                --_selected;
                float top = _selected * Theme::ITEM_HEIGHT;
                if (top < _scroll_y) _scroll_y = top;
                _filename = _entries[_selected].name;
                _cursor   = static_cast<int>(_filename.size());
            }
            return true;

        case GLFW_KEY_DOWN:
            if (_selected + 1 < static_cast<int>(_entries.size())) {
                ++_selected;
                float bottom = (_selected + 1) * Theme::ITEM_HEIGHT;
                float max_sc = bottom - _ly.list_h();
                if (_scroll_y < max_sc) _scroll_y = max_sc;
                _filename = _entries[_selected].name;
                _cursor   = static_cast<int>(_filename.size());
            }
            return true;
    }
    return true;  // consume all key events while open
}

bool FileDialog::handle_char(unsigned int codepoint) {
    if (_closed) return false;
    // Insert UTF-8 encoded codepoint at cursor
    char buf[5] = {};
    int  len = 0;
    if (codepoint < 0x80) {
        buf[len++] = static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        buf[len++] = static_cast<char>(0xC0 | (codepoint >> 6));
        buf[len++] = static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        buf[len++] = static_cast<char>(0xE0 | (codepoint >> 12));
        buf[len++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buf[len++] = static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    _filename.insert(_cursor, buf, len);
    _cursor += len;
    return true;
}

} // namespace bl_ui
