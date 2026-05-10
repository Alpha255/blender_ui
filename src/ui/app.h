#pragma once
#include "../render/gl_context.h"
#include "../render/roundbox.h"
#include "../render/font.h"
#include "../render/icon_atlas.h"
#include "menu_bar.h"
#include "viewport.h"       // Viewport3D
#include "confirm_dialog.h"
#include <bl_ui/menu_type.h>
#include <functional>
#include <string>

namespace bl_ui {

// ---------------------------------------------------------------------------
// App — application loop + GLFW event dispatch
// ---------------------------------------------------------------------------

class App {
public:
    App(int width, int height, const char* title);
    ~App() = default;

    // Non-copyable
    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    // Returns false if initialization failed.
    bool ready() const { return _ready; }

    // Main event loop.
    void run();

    MenuBar&      menu_bar()  { return _bar;      }
    MenuRegistry& registry()  { return _reg;      }
    Viewport3D&   viewport()  { return _viewport; }

    // Called when an operator is activated from any menu.
    void set_operator_callback(std::function<void(const std::string&)> cb);

    // Optional extra draw callback executed each frame after the grid and
    // before the menu bar, for custom overlay content in the viewport.
    void set_viewport_draw(std::function<void(float vp_w, float vp_h)> cb) {
        _viewport_draw = std::move(cb);
    }

    Roundbox&   roundbox()   { return _rb;    }
    Font&       font()       { return _font;  }
    IconAtlas&  icon_atlas() { return _icons; }

private:
    static void _cb_mouse_button(GLFWwindow*, int btn, int action, int mods);
    static void _cb_cursor_pos  (GLFWwindow*, double x, double y);
    static void _cb_key         (GLFWwindow*, int key, int sc, int action, int mods);
    static void _cb_framebuffer (GLFWwindow*, int w, int h);
    static void _cb_scroll      (GLFWwindow*, double dx, double dy);

    GLContext    _ctx;
    Roundbox     _rb;
    Font         _font;
    IconAtlas    _icons;
    MenuRegistry _reg;
    MenuBar      _bar;
    Viewport3D   _viewport;

    bool  _ready   = false;

    // Logical (window) size — used for UI coordinate system and mouse hit-testing.
    float _vp_w    = 800.f;
    float _vp_h    = 600.f;

    // Physical (framebuffer) size — used only for glViewport.
    int   _fb_w    = 800;
    int   _fb_h    = 600;

    // DPI scale from glfwGetWindowContentScale (typically 1.0, 1.25, 1.5, 2.0).
    float _content_scale = 1.f;

    float _mouse_x = 0.f;
    float _mouse_y = 0.f;

    std::function<void(const std::string&)>     _op_cb;
    std::function<void(float vp_w, float vp_h)> _viewport_draw;

    // Active blocking dialog (nullptr when no dialog is open).
    std::unique_ptr<ConfirmDialog> _confirm;

    // Internal operator dispatcher — intercepts built-in ops before forwarding.
    void _on_operator(const std::string& op);
};

} // namespace bl_ui
