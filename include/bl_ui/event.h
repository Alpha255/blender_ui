#pragma once
#include <cstdint>

namespace bl_ui {

// Maps to wmEvent.type in source/blender/makesdna/DNA_windowmanager_types.h:344
enum class EventType : uint16_t {
    NONE = 0,

    // Mouse
    LEFTMOUSE   = 1,
    MIDDLEMOUSE = 2,
    RIGHTMOUSE  = 3,

    // Special keys
    ESC   = 10,
    RET   = 11,
    SPACE = 12,
    TAB   = 13,
    BACK  = 14,
    DEL   = 15,

    // Arrow keys
    LEFT  = 20,
    RIGHT = 21,
    UP    = 22,
    DOWN  = 23,

    // Letters A-Z
    A = 30, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Digits 0-9
    ZERO = 60, ONE, TWO, THREE, FOUR,
    FIVE, SIX, SEVEN, EIGHT, NINE,

    // Function keys
    F1 = 80, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

enum class EventValue : uint8_t {
    PRESS    = 0,
    RELEASE  = 1,
    CLICK    = 2,
    DBL_CLICK= 3,
    ANY      = 255,
};

struct Event {
    EventType  type  = EventType::NONE;
    EventValue value = EventValue::PRESS;
    float      x     = 0.f;
    float      y     = 0.f;
    bool       shift = false;
    bool       ctrl  = false;
    bool       alt   = false;

    bool operator==(const Event& o) const {
        return type == o.type && value == o.value
            && shift == o.shift && ctrl == o.ctrl && alt == o.alt;
    }
    bool operator!=(const Event& o) const { return !(*this == o); }
};

// Map GLFW key code to EventType (returns NONE if unknown)
EventType event_type_from_glfw_key(int glfw_key);

// Map GLFW mouse button to EventType (returns NONE if unknown)
EventType event_type_from_glfw_button(int glfw_button);

} // namespace bl_ui
