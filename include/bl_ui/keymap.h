#pragma once
#include "event.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace bl_ui {

// ---------------------------------------------------------------------------
// KeyMapItem — single binding
// Mirrors wmKeyMapItem in DNA_windowmanager_types.h:344
// ---------------------------------------------------------------------------

struct KeyMapItem {
    std::string idname;
    EventType   type   = EventType::NONE;
    EventValue  value  = EventValue::PRESS;
    bool        shift  = false;
    bool        ctrl   = false;
    bool        alt    = false;
    std::string kind;   // "operator" | "menu"
    std::unordered_map<std::string, std::string> props;

    bool matches(const Event& ev) const;
};

// ---------------------------------------------------------------------------
// KeyMap — named set of items (one per space/region)
// Mirrors wmKeyMap in DNA_windowmanager_types.h:418
// ---------------------------------------------------------------------------

class KeyMap {
public:
    KeyMap(std::string_view name,
           std::string_view space_type  = "",
           std::string_view region_type = "");

    // WM_keymap_add_item
    KeyMapItem& add_item(std::string_view idname, EventType type,
                         EventValue value = EventValue::PRESS,
                         bool shift = false, bool ctrl = false, bool alt = false);

    // WM_keymap_add_menu
    KeyMapItem& add_menu(std::string_view menu_idname, EventType type,
                         EventValue value = EventValue::PRESS,
                         bool shift = false, bool ctrl = false, bool alt = false);

    // Returns first matching item or nullptr
    const KeyMapItem* find_match(const Event& ev) const;

    const std::string& name() const { return _name; }
    const std::vector<KeyMapItem>& items() const { return _items; }

private:
    std::string _name, _space_type, _region_type;
    std::vector<KeyMapItem> _items;
};

// ---------------------------------------------------------------------------
// KeyConfig — collection of keymaps
// ---------------------------------------------------------------------------

class KeyConfig {
public:
    explicit KeyConfig(std::string_view name = "Default");

    KeyMap& ensure(std::string_view name,
                   std::string_view space_type  = "",
                   std::string_view region_type = "");

    KeyMap*       find(std::string_view name);
    const KeyMap* find(std::string_view name) const;

    // Search all keymaps for first match
    const KeyMapItem* find_match(const Event& ev) const;

private:
    std::string _name;
    std::vector<KeyMap> _keymaps;
};

} // namespace bl_ui
