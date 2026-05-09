#pragma once
#include "layout.h"
#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <any>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Context — arbitrary key/value bag passed through draw callbacks
// ---------------------------------------------------------------------------

class Context {
public:
    template<typename T>
    void set(std::string_view key, T&& val) {
        _data[std::string(key)] = std::forward<T>(val);
    }

    template<typename T>
    T get(std::string_view key, T def = T{}) const {
        auto it = _data.find(std::string(key));
        if (it == _data.end()) return def;
        try { return std::any_cast<T>(it->second); }
        catch (...) { return def; }
    }

    bool has(std::string_view key) const {
        return _data.count(std::string(key)) > 0;
    }

private:
    std::unordered_map<std::string, std::any> _data;
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

struct MenuType;
struct Menu;

// ---------------------------------------------------------------------------
// MenuType — registered descriptor for a menu
// Mirrors BKE_screen.hh:656 struct MenuType
// ---------------------------------------------------------------------------

struct MenuType {
    std::string idname;
    std::string label;
    std::string description;

    // poll(ctx, mt) → false means menu is greyed-out / hidden
    std::function<bool(const Context&, const MenuType&)> poll;

    // draw(ctx, menu) → populates menu.layout
    std::function<void(const Context&, Menu&)> draw;

    bool check_poll(const Context& ctx) const;

    // Invoke draw callback; returns a Menu with populated layout.
    // Caller must keep the MenuType alive while using the menu.
    Menu invoke_draw(const Context& ctx) const;
};

// ---------------------------------------------------------------------------
// Menu — transient object passed to draw callbacks
// ---------------------------------------------------------------------------

struct Menu {
    const MenuType* type   = nullptr;
    Layout          layout;
};

// ---------------------------------------------------------------------------
// MenuRegistry — maps idname → MenuType
// Mirrors wm_menu_type.cc  WM_menutype_*
// ---------------------------------------------------------------------------

class MenuRegistry {
public:
    void add(std::unique_ptr<MenuType> mt);

    // Returns nullptr and optionally prints warning when not found
    MenuType*       find(std::string_view idname, bool quiet = false);
    const MenuType* find(std::string_view idname, bool quiet = false) const;

    void remove(std::string_view idname);
    void clear();

    bool        contains(std::string_view idname) const;
    std::size_t size() const { return _map.size(); }

    // Iterate all registered types
    std::vector<MenuType*> all();

private:
    std::unordered_map<std::string, std::unique_ptr<MenuType>> _map;
};

// Global default registry (mirrors G_MAIN MenuType list)
extern MenuRegistry g_menu_registry;

} // namespace bl_ui
