#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <unordered_map>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Layout item types
// Mirrors source/blender/editors/interface/interface_layout.cc:3010-3059
// ---------------------------------------------------------------------------

struct OperatorItem {
    std::string idname;
    std::string text;
    std::string shortcut;      // keyboard shortcut hint (right-aligned, dimmed)
    int         icon_id = 0;   // bl_ui::BLIcon value; 0 = no icon
    std::unordered_map<std::string, std::string> props;
};

struct MenuRefItem {
    std::string menu_idname;
    std::string text;
    int         icon_id = 0;   // bl_ui::BLIcon value; 0 = no icon
};

struct SeparatorItem {};

struct LabelItem {
    std::string text;
};

using LayoutItem = std::variant<OperatorItem, MenuRefItem, SeparatorItem, LabelItem>;

// ---------------------------------------------------------------------------
// Layout — collects items during draw callback
// ---------------------------------------------------------------------------

class Layout {
public:
    // Add an operator button.  Returns ref to the stored item.
    OperatorItem& operator_(std::string_view idname, std::string_view text = "",
                            int icon_id = 0, std::string_view shortcut = "");

    // Add a submenu reference.
    MenuRefItem&  menu(std::string_view menu_idname, std::string_view text = "",
                       int icon_id = 0);

    // Add a separator line.
    void separator();

    // Add a non-clickable label.
    void label(std::string_view text);

    const std::vector<LayoutItem>& items() const { return _items; }
    void clear() { _items.clear(); }

private:
    std::vector<LayoutItem> _items;
};

} // namespace bl_ui
