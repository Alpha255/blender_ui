#include "app.h"
#include "../render/theme.h"
#include "../render/icon_atlas.h"
#include <bl_ui/icons.h>
#include <iostream>

namespace bl_ui {

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------

App::App(int width, int height, const char* title) {
    if (!_ctx.init(width, height, title)) return;
    if (!_ctx.init_gl())                  return;

    // Query content scale (HiDPI factor, e.g. 1.5 on 150% Windows scaling)
    _ctx.get_content_scale(_content_scale, _content_scale);

    // Compute layout metrics using Blender's WM_window_dpi_set_userdef formula:
    // widget_unit = round(18 * scale_factor) + 2 * pixelsize
    // This is NOT simple multiplication — at 1.5x it gives 29, not 30.
    Theme::set_ui_scale(_content_scale);

    // Logical (window) size — this is the UI coordinate system.
    // On HiDPI the framebuffer is content_scale× larger, but the window size
    // and mouse coordinates are always in logical pixels.
    int ww, wh;
    _ctx.get_window_size(ww, wh);
    _vp_w = float(ww);
    _vp_h = float(wh);

    // Physical framebuffer size — only used for glViewport.
    _ctx.get_framebuffer_size(_fb_w, _fb_h);

    if (!_rb.init())       { std::cerr << "[bl_ui] Roundbox init failed\n";   return; }
    if (!_viewport.init()) { std::cerr << "[bl_ui] Viewport3D init failed\n"; return; }

    // Blender formula: U.dpi = getDPIHint()*ui_scale*(72/96); scale_factor = U.dpi/72 = ui_scale
    // Font px = 11 * scale_factor.  At 100% Windows (ui_scale=1): 11px.
    // Our stb_truetype has no hinting — FreeType's hinting makes strokes snap to whole
    // pixels, appearing bolder/larger. Compensate by rendering ~17% larger (≈13px at 1×).
    // Equivalent to Blender at ui_scale≈1.17 or "Resolution Scale" ≈ 1.17 in Preferences.
    if (!_font.load(Theme::FONT_SIZE_PT, 96.f, _content_scale)) {
        std::cerr << "[bl_ui] Font load failed\n"; return;
    }

    // Icon atlas — load SVGs from the Blender source tree if present.
    // Falls back gracefully: icons are optional (no icon is drawn when atlas is not ready).
    {
        const std::string svg_dir = "E:/SourceCode/Graph/blender_ui/assets/icons";
        if (!_icons.init(svg_dir, 16.f, _content_scale)) {
            std::cerr << "[bl_ui] IconAtlas init failed (icons will be skipped)\n";
        }
        _icons.set_viewport(_vp_w, _vp_h);
    }

    // All rendering uses logical (window) pixel coordinates.
    _rb.set_viewport(_vp_w, _vp_h);
    _font.set_viewport(_vp_w, _vp_h);
    _bar.set_dependencies(&_rb, &_font, &_reg, &_icons);

    // Install our internal dispatcher so the App can intercept built-in operators
    // (e.g. wm.quit_blender) before forwarding to the user-supplied callback.
    _bar.set_operator_callback([this](const std::string& op) { _on_operator(op); });

    // Viewport: initialise size and rendering dependencies.
    _viewport.set_viewport(_vp_w, _vp_h);
    _viewport.set_dependencies(&_rb, &_font);

    glfwSetWindowUserPointer(_ctx.window(), this);
    glfwSetMouseButtonCallback(_ctx.window(), _cb_mouse_button);
    glfwSetCursorPosCallback  (_ctx.window(), _cb_cursor_pos);
    glfwSetKeyCallback        (_ctx.window(), _cb_key);
    glfwSetCharCallback       (_ctx.window(), _cb_char);
    glfwSetScrollCallback     (_ctx.window(), _cb_scroll);
    glfwSetFramebufferSizeCallback(_ctx.window(), _cb_framebuffer);
    // Also listen for window resize to update logical size
    glfwSetWindowSizeCallback(_ctx.window(), [](GLFWwindow* win, int w, int h) {
        auto* app = static_cast<App*>(glfwGetWindowUserPointer(win));
        if (!app) return;
        app->_vp_w = float(w);
        app->_vp_h = float(h);
        app->_rb.set_viewport(float(w), float(h));
        app->_font.set_viewport(float(w), float(h));
        app->_icons.set_viewport(float(w), float(h));
        app->_viewport.set_viewport(float(w), float(h));
        if (app->_confirm)      app->_confirm->set_viewport(float(w), float(h));
        if (app->_file_dialog)  app->_file_dialog->set_viewport(float(w), float(h));
        if (app->_about)        app->_about->set_viewport(float(w), float(h));
    });

    _ready = true;
}

void App::set_operator_callback(std::function<void(const std::string&)> cb) {
    _op_cb = std::move(cb);
    // Don't overwrite _bar's callback — it is already wired to _on_operator().
}

// Internal dispatcher — intercepts built-in operators, forwards the rest.
void App::_on_operator(const std::string& op) {
    if (op == "wm.quit_blender") {
        // Blender: wm_quit_blender_invoke → WM_operator_confirm_message_ex
        // "Quit Blender?", ICON_WARNING, WM_YES_NO → [Cancel] [Quit Blender]
        _confirm = std::make_unique<ConfirmDialog>(
            "Quit Blender?",
            std::vector<ConfirmDialog::Button>{
                {"Cancel",       true },
                {"Quit Blender", false},
            },
            _vp_w, _vp_h,
            &_rb, &_font, &_icons,
            ICON_ERROR    // closest available to Blender's ICON_WARNING
        );
        return;
    }
    if (op == "wm.open_mainfile") {
        _file_dialog = std::make_unique<FileDialog>(_vp_w, _vp_h, &_rb, &_font, &_icons);
        return;
    }
    if (op == "wm.splash_about") {
        _about = std::make_unique<AboutDialog>(_vp_w, _vp_h, &_rb, &_font, &_icons);
        return;
    }
    if (_op_cb) _op_cb(op);
}

void App::run() {
    if (!_ready) return;

    while (!_ctx.should_close()) {
        _ctx.poll_events();

        // Set viewport to physical framebuffer for full-resolution rendering.
        glViewport(0, 0, _fb_w, _fb_h);

        RGBA bg = Theme::VIEWPORT_BG;
        glClearColor(bg.rf(), bg.gf(), bg.bf(), 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Viewport grid (below the menu bar).
        _viewport.draw(Theme::HEADER_H);

        // Optional custom overlay drawn on top of the grid.
        if (_viewport_draw) _viewport_draw(_vp_w, _vp_h);

        // Menu bar uses logical coordinate system (_vp_w, _vp_h).
        _bar.draw(_vp_w, _vp_h);

        // File dialog (full-screen, modal — drawn on top of viewport/bar).
        if (_file_dialog) {
            _file_dialog->draw();
            if (_file_dialog->is_closed()) {
                if (_file_dialog->is_confirmed() && _op_cb) {
                    _op_cb("wm.open_mainfile:" + _file_dialog->result_path());
                }
                _file_dialog.reset();
            }
        }

        // About dialog (modal).
        if (_about) {
            _about->draw();
            if (_about->is_closed()) _about.reset();
        }

        // Confirm dialog (modal — drawn on top of everything).
        if (_confirm) {
            _confirm->draw();
            if (_confirm->is_closed()) {
                if (_confirm->result() >= 1) {
                    // index 1 = "Quit Blender" (the confirm button)
                    glfwSetWindowShouldClose(_ctx.window(), GLFW_TRUE);
                }
                _confirm.reset();
            }
        }

        _ctx.swap_buffers();
    }
}

// ---------------------------------------------------------------------------
// GLFW callbacks
// ---------------------------------------------------------------------------

App* get_app(GLFWwindow* win) {
    return static_cast<App*>(glfwGetWindowUserPointer(win));
}

void App::_cb_framebuffer(GLFWwindow* win, int w, int h) {
    auto* app = get_app(win);
    if (!app) return;
    // Physical framebuffer changed — update for glViewport only.
    app->_fb_w = w;
    app->_fb_h = h;
    // Logical size may also change (e.g. window dragged to a different DPI monitor)
    int ww, wh;
    glfwGetWindowSize(win, &ww, &wh);
    app->_vp_w = float(ww);
    app->_vp_h = float(wh);
    app->_rb.set_viewport(float(ww), float(wh));
    app->_font.set_viewport(float(ww), float(wh));
    app->_icons.set_viewport(float(ww), float(wh));
    app->_viewport.set_viewport(float(ww), float(wh));
    if (app->_confirm)      app->_confirm->set_viewport(float(ww), float(wh));
    if (app->_file_dialog)  app->_file_dialog->set_viewport(float(ww), float(wh));
    if (app->_about)        app->_about->set_viewport(float(ww), float(wh));
    // Update content scale in case the window moved to a different monitor
    float sx = 1.f, sy = 1.f;
    glfwGetWindowContentScale(win, &sx, &sy);
    app->_content_scale = sx;
    Theme::set_ui_scale(sx);
}

void App::_cb_cursor_pos(GLFWwindow* win, double x, double y) {
    auto* app = get_app(win);
    if (!app) return;
    app->_mouse_x = float(x);
    app->_mouse_y = float(y);
    if (app->_about) {
        app->_about->handle_mouse(float(x), float(y), false, false);
        return;
    }
    if (app->_file_dialog) {
        app->_file_dialog->handle_mouse(float(x), float(y), false, false);
        return;
    }
    if (app->_confirm) {
        app->_confirm->handle_mouse(float(x), float(y), false, false);
        return;
    }
    app->_bar.handle_mouse_move(float(x), float(y));
    app->_viewport.handle_mouse_move(float(x), float(y));
}

void App::_cb_mouse_button(GLFWwindow* win, int btn, int action, int mods) {
    auto* app = get_app(win);
    if (!app) return;
    bool pressed  = (action == GLFW_PRESS);
    bool released = (action == GLFW_RELEASE);
    float mx = app->_mouse_x, my = app->_mouse_y;

    // About dialog consumes all mouse input while open.
    if (app->_about) {
        if (btn == GLFW_MOUSE_BUTTON_LEFT)
            app->_about->handle_mouse(mx, my, pressed, released);
        return;
    }

    // File dialog consumes all mouse input while open.
    if (app->_file_dialog) {
        if (btn == GLFW_MOUSE_BUTTON_LEFT)
            app->_file_dialog->handle_mouse(mx, my, pressed, released);
        return;
    }

    // Confirm dialog consumes all mouse input while open.
    if (app->_confirm) {
        if (btn == GLFW_MOUSE_BUTTON_LEFT)
            app->_confirm->handle_mouse(mx, my, pressed, released);
        return;
    }

    if (btn == GLFW_MOUSE_BUTTON_LEFT) {
        app->_bar.handle_mouse_button(mx, my, pressed, released);
    } else if (btn == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (my > Theme::HEADER_H) {
            app->_viewport.handle_mouse_button(mx, my, pressed, released, btn, mods);
        }
    }
}

void App::_cb_scroll(GLFWwindow* win, double /*dx*/, double dy) {
    auto* app = get_app(win);
    if (!app) return;
    if (app->_file_dialog) {
        app->_file_dialog->handle_scroll(float(dy));
        return;
    }
    if (app->_confirm) return;
    if (app->_mouse_y > Theme::HEADER_H && !app->_bar.has_open_popup()) {
        app->_viewport.handle_scroll(app->_mouse_x, app->_mouse_y, float(dy));
    }
}

void App::_cb_key(GLFWwindow* win, int key, int /*sc*/, int action, int mods) {
    auto* app = get_app(win);
    if (!app || action == GLFW_RELEASE) return;
    if (app->_about) {
        app->_about->handle_key(key, mods);
        return;
    }
    if (app->_file_dialog) {
        app->_file_dialog->handle_key(key, mods);
        return;
    }
    if (app->_confirm) {
        app->_confirm->handle_key(key, mods);
        return;
    }
    app->_bar.handle_key(key, mods);
}

void App::_cb_char(GLFWwindow* win, unsigned int codepoint) {
    auto* app = get_app(win);
    if (!app) return;
    if (app->_file_dialog)
        app->_file_dialog->handle_char(codepoint);
}

} // namespace bl_ui
