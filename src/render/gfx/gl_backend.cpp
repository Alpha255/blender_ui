#include "gl_backend.h"
#include "../gl_context.h"   // compile_shader() helper
#include <GLFW/glfw3.h>
#include <iostream>

namespace bl_ui::gfx {

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

GlBackend::GlBackend(GLFWwindow* window) : _window(window) {}

GlBackend::~GlBackend() { shutdown(); }

// ---------------------------------------------------------------------------
// init / shutdown
// ---------------------------------------------------------------------------

bool GlBackend::init(void* /*window*/, int fb_w, int fb_h) {
    // GLEW must already be initialised by GLContext::init_gl() before this.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, fb_w, fb_h);
    return true;
}

void GlBackend::shutdown() {
    for (auto& [id, e] : _shaders)  glDeleteProgram(e.prog);
    for (auto& [id, e] : _buffers)  glDeleteBuffers(1, &e.vbo);
    for (auto& [id, e] : _textures) glDeleteTextures(1, &e.tex);
    for (auto& [k,  v] : _vaos)     glDeleteVertexArrays(1, &v);
    _shaders.clear();
    _buffers.clear();
    _textures.clear();
    _vaos.clear();
    _uniforms.clear();
    _cur_shader = 0;
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void GlBackend::begin_frame(int fb_w, int fb_h) {
    glViewport(0, 0, fb_w, fb_h);
}

void GlBackend::clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GlBackend::end_frame() {
    // Buffer swap is handled by glfwSwapBuffers() in App::run().
}

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------

ShaderHandle GlBackend::create_shader(const char* vs, const char* fs) {
    GLuint prog = bl_ui::compile_shader(vs, fs);
    if (!prog) return 0;
    uint32_t id = _next_id++;
    _shaders[id] = { prog };
    return id;
}

BufferHandle GlBackend::create_buffer(const void* data, size_t bytes,
                                       bool dynamic) {
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, data,
                 dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    uint32_t id = _next_id++;
    _buffers[id] = { vbo, bytes };
    return id;
}

TextureHandle GlBackend::create_texture(int w, int h, PixelFormat fmt,
                                         FilterMode filter,
                                         const void* pixels) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    GLint  gl_filter = (filter == FilterMode::Nearest) ? GL_NEAREST : GL_LINEAR;
    GLenum gl_fmt    = (fmt == PixelFormat::R8) ? GL_RED  : GL_RGBA;
    GLenum gl_ifmt   = (fmt == PixelFormat::R8) ? GL_RED  : GL_RGBA8;

    glTexImage2D(GL_TEXTURE_2D, 0, gl_ifmt, w, h, 0,
                 gl_fmt, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    uint32_t id = _next_id++;
    _textures[id] = { tex, fmt };
    return id;
}

// ---------------------------------------------------------------------------
// Resource update
// ---------------------------------------------------------------------------

void GlBackend::update_buffer(BufferHandle buf, const void* data,
                               size_t bytes, size_t offset) {
    auto it = _buffers.find(buf);
    if (it == _buffers.end()) return;
    glBindBuffer(GL_ARRAY_BUFFER, it->second.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)offset,
                    (GLsizeiptr)bytes, data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GlBackend::update_texture(TextureHandle handle, int x, int y,
                                int w, int h, const void* pixels) {
    auto it = _textures.find(handle);
    if (it == _textures.end()) return;
    GLenum fmt = (it->second.fmt == PixelFormat::R8) ? GL_RED : GL_RGBA;
    glBindTexture(GL_TEXTURE_2D, it->second.tex);
    // pixels are tightly packed (no row-stride gap).
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h,
                    fmt, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// Resource destruction
// ---------------------------------------------------------------------------

void GlBackend::destroy_shader(ShaderHandle id) {
    auto it = _shaders.find(id);
    if (it == _shaders.end()) return;
    glDeleteProgram(it->second.prog);
    _shaders.erase(it);
    // Purge cached uniform locations for this shader.
    for (auto uit = _uniforms.begin(); uit != _uniforms.end(); ) {
        uit = (uit->first.shader == id) ? _uniforms.erase(uit) : std::next(uit);
    }
}

void GlBackend::destroy_buffer(BufferHandle id) {
    auto it = _buffers.find(id);
    if (it == _buffers.end()) return;
    glDeleteBuffers(1, &it->second.vbo);
    _buffers.erase(it);
    // Remove all VAOs that reference this buffer.
    for (auto vit = _vaos.begin(); vit != _vaos.end(); ) {
        if (vit->first.buf_id == id) {
            glDeleteVertexArrays(1, &vit->second);
            vit = _vaos.erase(vit);
        } else {
            ++vit;
        }
    }
}

void GlBackend::destroy_texture(TextureHandle id) {
    auto it = _textures.find(id);
    if (it == _textures.end()) return;
    glDeleteTextures(1, &it->second.tex);
    _textures.erase(it);
}

// ---------------------------------------------------------------------------
// Render state
// ---------------------------------------------------------------------------

void GlBackend::set_depth(bool test, bool write, bool always_pass) {
    if (test) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(always_pass ? GL_ALWAYS : GL_LESS);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(write ? GL_TRUE : GL_FALSE);
}

void GlBackend::set_blend_alpha(bool enabled) {
    if (enabled) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

// ---------------------------------------------------------------------------
// Shader binding & uniforms
// ---------------------------------------------------------------------------

void GlBackend::use_shader(ShaderHandle id) {
    auto it = _shaders.find(id);
    if (it == _shaders.end()) return;
    glUseProgram(it->second.prog);
    _cur_shader = id;
}

GLint GlBackend::_uniform_loc(const char* name) {
    UniformKey key{ _cur_shader, name };
    auto it = _uniforms.find(key);
    if (it != _uniforms.end()) return it->second;

    auto sit = _shaders.find(_cur_shader);
    if (sit == _shaders.end()) return -1;
    GLint loc = glGetUniformLocation(sit->second.prog, name);
    _uniforms[std::move(key)] = loc;
    return loc;
}

void GlBackend::uniform_1f(const char* n, float v)
    { glUniform1f(_uniform_loc(n), v); }

void GlBackend::uniform_2f(const char* n, float x, float y)
    { glUniform2f(_uniform_loc(n), x, y); }

void GlBackend::uniform_4f(const char* n, float x, float y, float z, float w)
    { glUniform4f(_uniform_loc(n), x, y, z, w); }

void GlBackend::uniform_m4(const char* n, const float* m)
    { glUniformMatrix4fv(_uniform_loc(n), 1, GL_FALSE, m); }

void GlBackend::uniform_1i(const char* n, int v)
    { glUniform1i(_uniform_loc(n), v); }

void GlBackend::uniform_2i(const char* n, int x, int y)
    { glUniform2i(_uniform_loc(n), x, y); }

void GlBackend::bind_texture(int unit, TextureHandle handle,
                               const char* sampler_name) {
    auto it = _textures.find(handle);
    if (it == _textures.end()) return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, it->second.tex);
    // Assign the sampler uniform to this texture unit.
    glUniform1i(_uniform_loc(sampler_name), unit);
}

// ---------------------------------------------------------------------------
// VAO management
// ---------------------------------------------------------------------------

uint32_t GlBackend::_layout_hash(const VertexLayout& l) noexcept {
    uint32_t h = (uint32_t)l.stride * 2654435761u ^ (uint32_t)l.attr_count;
    for (int i = 0; i < l.attr_count; ++i) {
        h ^= (uint32_t)l.attrs[i].location   * 2246822519u;
        h ^= (uint32_t)l.attrs[i].components * 3266489917u;
        h ^= (uint32_t)l.attrs[i].offset     * 374761393u;
    }
    return h;
}

GLuint GlBackend::_get_or_create_vao(BufferHandle buf,
                                      const VertexLayout& layout) {
    VaoKey key{ buf, _layout_hash(layout) };
    auto it = _vaos.find(key);
    if (it != _vaos.end()) return it->second;

    auto bit = _buffers.find(buf);
    if (bit == _buffers.end()) return 0;

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, bit->second.vbo);
    for (int i = 0; i < layout.attr_count; ++i) {
        const VertexAttr& a = layout.attrs[i];
        glVertexAttribPointer(a.location, a.components, GL_FLOAT, GL_FALSE,
                              layout.stride, (void*)(intptr_t)a.offset);
        glEnableVertexAttribArray(a.location);
    }
    glBindVertexArray(0);

    _vaos[key] = vao;
    return vao;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void GlBackend::draw_triangles(BufferHandle buf, const VertexLayout& layout,
                                int first, int count) {
    GLuint vao = _get_or_create_vao(buf, layout);
    if (!vao) return;
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, first, count);
    glBindVertexArray(0);
}

} // namespace bl_ui::gfx
