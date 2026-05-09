#include <bl_ui/menu_type.h>
#include <iostream>
#include <stdexcept>

namespace bl_ui {

// ---------------------------------------------------------------------------
// MenuType
// ---------------------------------------------------------------------------

bool MenuType::check_poll(const Context& ctx) const {
    if (!poll) return true;
    return poll(ctx, *this);
}

Menu MenuType::invoke_draw(const Context& ctx) const {
    Menu m;
    m.type = this;
    if (draw) draw(ctx, m);
    return m;
}

// ---------------------------------------------------------------------------
// MenuRegistry
// ---------------------------------------------------------------------------

void MenuRegistry::add(std::unique_ptr<MenuType> mt) {
    if (!mt) return;
    if (_map.count(mt->idname)) {
        throw std::runtime_error("MenuRegistry: duplicate idname '" + mt->idname + "'");
    }
    _map.emplace(mt->idname, std::move(mt));
}

MenuType* MenuRegistry::find(std::string_view idname, bool quiet) {
    auto it = _map.find(std::string(idname));
    if (it == _map.end()) {
        if (!quiet) std::cout << "Unknown menu type: '" << idname << "'\n";
        return nullptr;
    }
    return it->second.get();
}

const MenuType* MenuRegistry::find(std::string_view idname, bool quiet) const {
    auto it = _map.find(std::string(idname));
    if (it == _map.end()) {
        if (!quiet) std::cout << "Unknown menu type: '" << idname << "'\n";
        return nullptr;
    }
    return it->second.get();
}

void MenuRegistry::remove(std::string_view idname) {
    _map.erase(std::string(idname));
}

void MenuRegistry::clear() {
    _map.clear();
}

bool MenuRegistry::contains(std::string_view idname) const {
    return _map.count(std::string(idname)) > 0;
}

std::vector<MenuType*> MenuRegistry::all() {
    std::vector<MenuType*> out;
    out.reserve(_map.size());
    for (auto& [k, v] : _map) out.push_back(v.get());
    return out;
}

// Global registry instance
MenuRegistry g_menu_registry;

} // namespace bl_ui
