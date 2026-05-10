#pragma once
#include "../render/roundbox.h"
#include "../render/font.h"
#include "../render/icon_atlas.h"
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace bl_ui {

// ---------------------------------------------------------------------------
// FileDialog — Blender-style file browser dialog.
//
// Visual layout (matches space_file regions):
//
//  ┌───────────────────────────────────────────────────────┐
//  │ HEADER  [<][>][^][↺]  C: > Users > John > Documents  │  ← HEADER_H
//  ├──────────────┬────────────────────────────────────────┤
//  │ SIDEBAR      │  FILE LIST (scrollable)                │
//  │  System      │  📁 folder1                            │
//  │  Bookmarks   │  📄 file1.blend          1.2 MB        │
//  │              │                              ▐ scroll  │
//  ├──────────────┴────────────────────────────────────────┤
//  │ EXECUTE  File Name: [______________]  [Cancel] [Open] │  ← footer_h
//  └───────────────────────────────────────────────────────┘
//
// Reference: source/blender/editors/space_file/file_draw.cc
// ---------------------------------------------------------------------------

class FileDialog {
public:
    FileDialog(float vp_w, float vp_h,
               Roundbox* rb, Font* font, IconAtlas* icons = nullptr);

    void set_viewport(float w, float h);

    void draw();

    bool handle_mouse(float mx, float my, bool pressed, bool released);
    bool handle_scroll(float delta_y);
    bool handle_key(int glfw_key, int mods);
    bool handle_char(unsigned int codepoint);

    bool is_closed()    const { return _closed;    }
    bool is_confirmed() const { return _confirmed; }
    const std::string& result_path() const { return _result_path; }

private:
    // -----------------------------------------------------------------------
    // Layout (recomputed on resize or scale change)
    // -----------------------------------------------------------------------
    struct Layout {
        float vp_w, vp_h;
        float dlg_x, dlg_y;   // dialog top-left in viewport coords
        float dlg_w, dlg_h;   // dialog size
        float header_h;    // top navigation bar (relative within dialog)
        float sidebar_w;   // left bookmarks panel
        float footer_h;    // bottom execute region
        float scrollbar_w;

        float list_x()   const { return dlg_x + sidebar_w; }
        float list_y()   const { return dlg_y + header_h; }
        float list_w()   const { return dlg_w - sidebar_w - scrollbar_w; }
        float list_h()   const { return dlg_h - header_h - footer_h; }
        float footer_y() const { return dlg_y + dlg_h - footer_h; }
    };

    void _compute_layout();

    // -----------------------------------------------------------------------
    // File system
    // -----------------------------------------------------------------------
    struct Entry {
        fs::path    path;
        bool        is_dir;
        std::string name;
        std::string size_str;
    };

    void _navigate_to(const fs::path& path, bool push = true);
    void _go_parent();
    void _go_back();
    void _go_forward();

    static std::string _format_size(uintmax_t bytes);

    // -----------------------------------------------------------------------
    // Bookmarks (sidebar)
    // -----------------------------------------------------------------------
    struct Bookmark {
        std::string label;
        fs::path    path;
        bool        is_header;  // section heading — not clickable
    };

    void _init_bookmarks();

    // -----------------------------------------------------------------------
    // Drawing
    // -----------------------------------------------------------------------
    void _draw_header();
    void _draw_nav_btn(float x, float y, float sz, const char* label,
                       bool enabled, bool hovered);
    void _draw_sidebar();
    void _draw_file_list();
    void _draw_scrollbar();
    void _draw_footer();

    // -----------------------------------------------------------------------
    // Hit-testing
    // -----------------------------------------------------------------------
    int  _hit_file(float mx, float my) const;
    int  _hit_bookmark(float mx, float my) const;
    int  _hit_nav(float mx, float my) const;   // 0=back,1=fwd,2=parent,3=refresh

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    Layout _ly;

    fs::path           _current;
    std::vector<Entry> _entries;

    // Navigation history
    std::vector<fs::path> _history;
    int _hist_idx = -1;

    std::vector<Bookmark> _bookmarks;

    // Scroll
    float _scroll_y      = 0.f;
    bool  _sb_dragging   = false;
    float _sb_drag_y0    = 0.f;
    float _sb_drag_sc0   = 0.f;

    // Selection & hover
    int _selected   = -1;
    int _hv_file    = -1;
    int _hv_bm      = -1;
    int _hv_nav     = -1;

    // Double-click detection
    double _last_click_time = -1.0;
    int    _last_click_idx  = -1;

    // Filename text input
    std::string _filename;
    int         _cursor = 0;   // byte position within _filename

    // Path field edit mode
    bool        _path_editing = false;
    std::string _path_edit;
    int         _path_cursor = 0;

    // Close state
    bool        _closed    = false;
    bool        _confirmed = false;
    std::string _result_path;

    // Refs
    float      _vp_w, _vp_h;
    Roundbox*  _rb;
    Font*      _font;
    IconAtlas* _icons;
};

} // namespace bl_ui
