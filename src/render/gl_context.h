#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Shader helpers
// ---------------------------------------------------------------------------

// Compile a vertex+fragment shader pair; returns program ID or 0 on failure.
GLuint compile_shader(const char* vert_src, const char* frag_src);

// ---------------------------------------------------------------------------
// GLContext — owns the GLFW window and GL state
// ---------------------------------------------------------------------------

class GLContext {
public:
    GLContext() = default;
    ~GLContext();

    // Non-copyable
    GLContext(const GLContext&)            = delete;
    GLContext& operator=(const GLContext&) = delete;

    // Create a GLFW window.
    // opengl=true  → GL 3.3 Core context hints (default, for GlBackend)
    // opengl=false → GLFW_CLIENT_API=GLFW_NO_API (required for VkBackend)
    bool init(int width, int height, const char* title, bool opengl = true);

    // Must be called after context is current; initializes GLEW.
    bool init_gl();

    // Physical pixel dimensions of the framebuffer.
    void get_framebuffer_size(int& w, int& h) const;

    // Logical (screen-coordinate) window size.
    // On HiDPI: window_size * content_scale ≈ framebuffer_size
    void get_window_size(int& w, int& h) const;

    // DPI content scale (e.g. 1.5 at 150% Windows display scaling).
    void get_content_scale(float& sx, float& sy) const;

    void get_cursor_pos(double& x, double& y) const;

    bool should_close() const;
    void swap_buffers();
    void poll_events();

    GLFWwindow* window() const { return _win; }

private:
    GLFWwindow* _win        = nullptr;
    bool        _glfw_owner = false;
    bool        _opengl     = true;
};

} // namespace bl_ui
