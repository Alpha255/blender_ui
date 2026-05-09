#include <bl_ui/keymap.h>
// GL/glew.h must be included before glfw3.h on Windows
#include <GL/glew.h>
#include <GLFW/glfw3.h>

namespace bl_ui {

// ---------------------------------------------------------------------------
// EventType ↔ GLFW key mapping
// ---------------------------------------------------------------------------

EventType event_type_from_glfw_key(int k) {
    using E = EventType;
    switch (k) {
        case GLFW_KEY_ESCAPE:       return E::ESC;
        case GLFW_KEY_ENTER:        return E::RET;
        case GLFW_KEY_KP_ENTER:     return E::RET;
        case GLFW_KEY_SPACE:        return E::SPACE;
        case GLFW_KEY_TAB:          return E::TAB;
        case GLFW_KEY_BACKSPACE:    return E::BACK;
        case GLFW_KEY_DELETE:       return E::DEL;
        case GLFW_KEY_LEFT:         return E::LEFT;
        case GLFW_KEY_RIGHT:        return E::RIGHT;
        case GLFW_KEY_UP:           return E::UP;
        case GLFW_KEY_DOWN:         return E::DOWN;
        case GLFW_KEY_A:            return E::A;
        case GLFW_KEY_B:            return E::B;
        case GLFW_KEY_C:            return E::C;
        case GLFW_KEY_D:            return E::D;
        case GLFW_KEY_E:            return E::E;
        case GLFW_KEY_F:            return E::F;
        case GLFW_KEY_G:            return E::G;
        case GLFW_KEY_H:            return E::H;
        case GLFW_KEY_I:            return E::I;
        case GLFW_KEY_J:            return E::J;
        case GLFW_KEY_K:            return E::K;
        case GLFW_KEY_L:            return E::L;
        case GLFW_KEY_M:            return E::M;
        case GLFW_KEY_N:            return E::N;
        case GLFW_KEY_O:            return E::O;
        case GLFW_KEY_P:            return E::P;
        case GLFW_KEY_Q:            return E::Q;
        case GLFW_KEY_R:            return E::R;
        case GLFW_KEY_S:            return E::S;
        case GLFW_KEY_T:            return E::T;
        case GLFW_KEY_U:            return E::U;
        case GLFW_KEY_V:            return E::V;
        case GLFW_KEY_W:            return E::W;
        case GLFW_KEY_X:            return E::X;
        case GLFW_KEY_Y:            return E::Y;
        case GLFW_KEY_Z:            return E::Z;
        case GLFW_KEY_0:            return E::ZERO;
        case GLFW_KEY_1:            return E::ONE;
        case GLFW_KEY_2:            return E::TWO;
        case GLFW_KEY_3:            return E::THREE;
        case GLFW_KEY_4:            return E::FOUR;
        case GLFW_KEY_5:            return E::FIVE;
        case GLFW_KEY_6:            return E::SIX;
        case GLFW_KEY_7:            return E::SEVEN;
        case GLFW_KEY_8:            return E::EIGHT;
        case GLFW_KEY_9:            return E::NINE;
        case GLFW_KEY_F1:           return E::F1;
        case GLFW_KEY_F2:           return E::F2;
        case GLFW_KEY_F3:           return E::F3;
        case GLFW_KEY_F4:           return E::F4;
        case GLFW_KEY_F5:           return E::F5;
        case GLFW_KEY_F6:           return E::F6;
        case GLFW_KEY_F7:           return E::F7;
        case GLFW_KEY_F8:           return E::F8;
        case GLFW_KEY_F9:           return E::F9;
        case GLFW_KEY_F10:          return E::F10;
        case GLFW_KEY_F11:          return E::F11;
        case GLFW_KEY_F12:          return E::F12;
        default:                    return E::NONE;
    }
}

EventType event_type_from_glfw_button(int btn) {
    switch (btn) {
        case GLFW_MOUSE_BUTTON_LEFT:   return EventType::LEFTMOUSE;
        case GLFW_MOUSE_BUTTON_MIDDLE: return EventType::MIDDLEMOUSE;
        case GLFW_MOUSE_BUTTON_RIGHT:  return EventType::RIGHTMOUSE;
        default:                       return EventType::NONE;
    }
}

// ---------------------------------------------------------------------------
// KeyMapItem
// ---------------------------------------------------------------------------

bool KeyMapItem::matches(const Event& ev) const {
    if (type  != ev.type)  return false;
    if (value != ev.value && value != EventValue::ANY) return false;
    if (shift != ev.shift) return false;
    if (ctrl  != ev.ctrl)  return false;
    if (alt   != ev.alt)   return false;
    return true;
}

// ---------------------------------------------------------------------------
// KeyMap
// ---------------------------------------------------------------------------

KeyMap::KeyMap(std::string_view name,
               std::string_view space_type,
               std::string_view region_type)
    : _name(name)
    , _space_type(space_type)
    , _region_type(region_type)
{}

KeyMapItem& KeyMap::add_item(std::string_view idname, EventType type,
                             EventValue value, bool shift, bool ctrl, bool alt) {
    KeyMapItem item;
    item.idname = std::string(idname);
    item.type   = type;
    item.value  = value;
    item.shift  = shift;
    item.ctrl   = ctrl;
    item.alt    = alt;
    item.kind   = "operator";
    _items.push_back(std::move(item));
    return _items.back();
}

KeyMapItem& KeyMap::add_menu(std::string_view menu_idname, EventType type,
                             EventValue value, bool shift, bool ctrl, bool alt) {
    KeyMapItem& item = add_item(menu_idname, type, value, shift, ctrl, alt);
    item.kind = "menu";
    return item;
}

const KeyMapItem* KeyMap::find_match(const Event& ev) const {
    for (const auto& item : _items) {
        if (item.matches(ev)) return &item;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// KeyConfig
// ---------------------------------------------------------------------------

KeyConfig::KeyConfig(std::string_view name) : _name(name) {}

KeyMap& KeyConfig::ensure(std::string_view name,
                          std::string_view space_type,
                          std::string_view region_type) {
    for (auto& km : _keymaps) {
        if (km.name() == name) return km;
    }
    _keymaps.emplace_back(name, space_type, region_type);
    return _keymaps.back();
}

KeyMap* KeyConfig::find(std::string_view name) {
    for (auto& km : _keymaps) {
        if (km.name() == name) return &km;
    }
    return nullptr;
}

const KeyMap* KeyConfig::find(std::string_view name) const {
    for (const auto& km : _keymaps) {
        if (km.name() == name) return &km;
    }
    return nullptr;
}

const KeyMapItem* KeyConfig::find_match(const Event& ev) const {
    for (const auto& km : _keymaps) {
        if (auto* item = km.find_match(ev)) return item;
    }
    return nullptr;
}

} // namespace bl_ui
