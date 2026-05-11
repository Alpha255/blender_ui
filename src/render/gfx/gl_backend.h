#pragma once
#include "backend.h"
#include <GL/glew.h>
#include <unordered_map>
#include <string>

struct GLFWwindow;

namespace bl_ui::gfx {

// ---------------------------------------------------------------------------
// GlBackend — OpenGL 3.3 Core Profile implementation of Backend.
//
// Resource management:
//   ShaderHandle  → GLuint shader program
//   BufferHandle  → GLuint VBO
//   TextureHandle → GLuint texture object
//
// VAO caching: a VAO is created on the first draw_triangles() call for each
// unique (BufferHandle, vertex-layout-hash) pair and reused thereafter.
// Uniform location caching: glGetUniformLocation results are stored keyed by
// (shader-id, name) and never re-queried.
// ---------------------------------------------------------------------------
class GlBackend final : public Backend {
public:
    explicit GlBackend(GLFWwindow* window);
    ~GlBackend() override;

    bool init(void* window, int fb_w, int fb_h) override;
    void shutdown() override;

    void begin_frame(int fb_w, int fb_h) override;
    void clear(float r, float g, float b, float a) override;
    void end_frame() override;

    ShaderHandle  create_shader (const char* vs, const char* fs) override;
    BufferHandle  create_buffer (const void* data, size_t bytes,
                                  bool dynamic) override;
    TextureHandle create_texture(int w, int h, PixelFormat fmt,
                                  FilterMode filter,
                                  const void* pixels) override;

    void update_buffer (BufferHandle, const void* data,
                         size_t bytes, size_t offset) override;
    void update_texture(TextureHandle, int x, int y, int w, int h,
                         const void* pixels) override;

    void destroy_shader (ShaderHandle)  override;
    void destroy_buffer (BufferHandle)  override;
    void destroy_texture(TextureHandle) override;

    void set_blend_alpha(bool enabled) override;
    void set_depth(bool test, bool write, bool always_pass) override;

    void use_shader   (ShaderHandle) override;
    void uniform_1f   (const char* name, float v) override;
    void uniform_2f   (const char* name, float x, float y) override;
    void uniform_4f   (const char* name, float x, float y,
                        float z, float w) override;
    void uniform_m4   (const char* name, const float* col_major) override;
    void uniform_1i   (const char* name, int v) override;
    void uniform_2i   (const char* name, int x, int y) override;
    void bind_texture (int unit, TextureHandle,
                        const char* sampler_name) override;
    void draw_triangles(BufferHandle buf, const VertexLayout& layout,
                         int first, int count) override;

private:
    GLFWwindow* _window = nullptr;

    // Per-resource metadata
    struct ShaderEntry  { GLuint prog; };
    struct BufferEntry  { GLuint vbo; size_t size; };
    struct TextureEntry { GLuint tex; PixelFormat fmt; };

    // VAO cache: one VAO per (buffer, vertex-layout) pair.
    struct VaoKey {
        uint32_t buf_id;
        uint32_t layout_hash;
        bool operator==(const VaoKey& o) const noexcept {
            return buf_id == o.buf_id && layout_hash == o.layout_hash;
        }
    };
    struct VaoKeyHash {
        size_t operator()(const VaoKey& k) const noexcept {
            return std::hash<uint64_t>{}(
                (uint64_t)k.buf_id << 32 | k.layout_hash);
        }
    };

    // Uniform location cache: (shader-id, name) → GLint location.
    struct UniformKey {
        ShaderHandle shader;
        std::string  name;
        bool operator==(const UniformKey& o) const noexcept {
            return shader == o.shader && name == o.name;
        }
    };
    struct UniformKeyHash {
        size_t operator()(const UniformKey& k) const noexcept {
            return std::hash<uint32_t>{}(k.shader) ^
                   std::hash<std::string>{}(k.name) * 2654435761u;
        }
    };

    uint32_t _next_id = 1;
    std::unordered_map<ShaderHandle,  ShaderEntry>              _shaders;
    std::unordered_map<BufferHandle,  BufferEntry>              _buffers;
    std::unordered_map<TextureHandle, TextureEntry>             _textures;
    std::unordered_map<VaoKey,        GLuint, VaoKeyHash>       _vaos;
    std::unordered_map<UniformKey,    GLint,  UniformKeyHash>   _uniforms;

    ShaderHandle _cur_shader = 0;

    GLint    _uniform_loc(const char* name);
    GLuint   _get_or_create_vao(BufferHandle, const VertexLayout&);
    static uint32_t _layout_hash(const VertexLayout&) noexcept;
};

} // namespace bl_ui::gfx
