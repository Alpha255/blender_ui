#include "gl_context.h"
#include <iostream>
#include <vector>

namespace bl_ui {

// ---------------------------------------------------------------------------
// Shader utilities
// ---------------------------------------------------------------------------

static GLuint compile_stage(GLenum stage, const char* src) {
    GLuint sh = glCreateShader(stage);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len + 1);
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        std::cerr << "[bl_ui] Shader compile error:\n" << log.data() << "\n";
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint compile_shader(const char* vert_src, const char* frag_src) {
    GLuint vs = compile_stage(GL_VERTEX_SHADER,   vert_src);
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, frag_src);
    if (!vs || !fs) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len + 1);
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        std::cerr << "[bl_ui] Shader link error:\n" << log.data() << "\n";
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ---------------------------------------------------------------------------
// GLContext
// ---------------------------------------------------------------------------

static void glfw_error_cb(int /*code*/, const char* desc) {
    std::cerr << "[bl_ui] GLFW error: " << desc << "\n";
}

GLContext::~GLContext() {
    if (_win) {
        glfwDestroyWindow(_win);
        _win = nullptr;
    }
    if (_glfw_owner) {
        glfwTerminate();
        _glfw_owner = false;
    }
}

bool GLContext::init(int width, int height, const char* title) {
    glfwSetErrorCallback(glfw_error_cb);

    if (!glfwInit()) {
        std::cerr << "[bl_ui] glfwInit failed\n";
        return false;
    }
    _glfw_owner = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4); // 4x MSAA

    _win = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!_win) {
        std::cerr << "[bl_ui] Failed to create GLFW window\n";
        return false;
    }

    glfwMakeContextCurrent(_win);
    glfwSwapInterval(1); // vsync

    return true;
}

bool GLContext::init_gl() {
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cerr << "[bl_ui] GLEW init failed: "
                  << glewGetErrorString(err) << "\n";
        return false;
    }
    // GLEW sometimes triggers a spurious GL_INVALID_ENUM; flush it
    glGetError();

    // Enable alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Enable multisampling
    glEnable(GL_MULTISAMPLE);

    return true;
}

void GLContext::get_framebuffer_size(int& w, int& h) const {
    if (_win) glfwGetFramebufferSize(_win, &w, &h);
}

void GLContext::get_window_size(int& w, int& h) const {
    if (_win) glfwGetWindowSize(_win, &w, &h);
}

void GLContext::get_content_scale(float& sx, float& sy) const {
    if (_win) glfwGetWindowContentScale(_win, &sx, &sy);
    else { sx = sy = 1.f; }
}

void GLContext::get_cursor_pos(double& x, double& y) const {
    if (_win) glfwGetCursorPos(_win, &x, &y);
}

bool GLContext::should_close() const {
    return _win && glfwWindowShouldClose(_win);
}

void GLContext::swap_buffers() {
    if (_win) glfwSwapBuffers(_win);
}

void GLContext::poll_events() {
    glfwPollEvents();
}

} // namespace bl_ui
