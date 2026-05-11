#pragma once
#include <cstddef>
#include <cstdint>

namespace bl_ui::gfx {

// ---------------------------------------------------------------------------
// Opaque GPU resource handles — 0 is always null/invalid.
// ---------------------------------------------------------------------------
using ShaderHandle  = uint32_t;
using BufferHandle  = uint32_t;
using TextureHandle = uint32_t;

enum class PixelFormat { R8, RGBA8 };
enum class FilterMode  { Nearest, Linear };

// ---------------------------------------------------------------------------
// Vertex attribute descriptor
// ---------------------------------------------------------------------------
struct VertexAttr {
    int location;    // shader attribute binding location
    int components;  // number of float components (1–4)
    int offset;      // byte offset within the vertex struct
};

// Vertex buffer layout — stride + all attribute descriptors.
struct VertexLayout {
    const VertexAttr* attrs;
    int               attr_count;
    int               stride;  // bytes per vertex
};

// ---------------------------------------------------------------------------
// Backend — abstract GPU interface.
//
// Both the OpenGL 3.3 and Vulkan implementations derive from this class.
// The API follows an immediate-mode style close to OpenGL so the GL backend
// maps directly; the Vulkan backend records into a command buffer internally
// and presents in end_frame().
// ---------------------------------------------------------------------------
class Backend {
public:
    virtual ~Backend() = default;

    // One-time initialisation. window is the native GLFW window pointer
    // (GLFWwindow*), used for GL context validation and Vulkan surface
    // creation respectively.
    virtual bool init(void* window, int fb_w, int fb_h) = 0;
    virtual void shutdown() = 0;

    // Frame lifecycle — must bracket all rendering calls.
    virtual void begin_frame(int fb_w, int fb_h) = 0;
    virtual void clear(float r, float g, float b, float a) = 0;
    // end_frame() submits the frame; for Vulkan this also presents.
    // For GL, glfwSwapBuffers() is still called by the caller after this.
    virtual void end_frame() = 0;

    // -----------------------------------------------------------------------
    // Resource creation — returns 0 on failure.
    // -----------------------------------------------------------------------

    // Compile vertex + fragment GLSL source into a shader program.
    // GL backend: compiles as-is.
    // Vulkan backend: expects Vulkan-compatible GLSL (push_constant blocks,
    // set/binding qualifiers), compiles to SPIR-V via shaderc.
    virtual ShaderHandle create_shader(const char* vert_glsl,
                                       const char* frag_glsl) = 0;

    // Allocate a GPU buffer. Pass data=nullptr to create an uninitialised
    // buffer (typically followed by update_buffer each frame).
    virtual BufferHandle create_buffer(const void* data, size_t bytes,
                                       bool dynamic) = 0;

    // Upload a 2-D texture.  pixels may be nullptr to allocate only.
    virtual TextureHandle create_texture(int w, int h, PixelFormat fmt,
                                         FilterMode filter,
                                         const void* pixels) = 0;

    // -----------------------------------------------------------------------
    // Resource update
    // -----------------------------------------------------------------------

    // Overwrite a region of a buffer starting at byte offset.
    virtual void update_buffer(BufferHandle, const void* data,
                                size_t bytes, size_t offset = 0) = 0;

    // Upload a tightly-packed sub-region of a texture (pixels have
    // stride = w * bytes_per_pixel, no row padding).
    virtual void update_texture(TextureHandle, int x, int y, int w, int h,
                                 const void* pixels) = 0;

    // -----------------------------------------------------------------------
    // Resource destruction
    // -----------------------------------------------------------------------
    virtual void destroy_shader (ShaderHandle)  = 0;
    virtual void destroy_buffer (BufferHandle)  = 0;
    virtual void destroy_texture(TextureHandle) = 0;

    // -----------------------------------------------------------------------
    // Rendering state
    // -----------------------------------------------------------------------

    // Enable/disable standard alpha blending (src_alpha, one_minus_src_alpha).
    virtual void set_blend_alpha(bool enabled) = 0;

    // Depth buffer state.
    //   test      — enable depth test (GL: GL_DEPTH_TEST)
    //   write     — enable depth writes (GL: glDepthMask)
    //   always_pass — true: compare op = ALWAYS (used by Grid to write depth
    //                 unconditionally); false: compare op = LESS.
    virtual void set_depth(bool test, bool write, bool always_pass = false) = 0;

    // Bind a shader program; subsequent uniform_* calls target this shader.
    virtual void use_shader(ShaderHandle) = 0;

    // Set uniform values on the currently bound shader.
    // GL backend: resolves uniform locations lazily and caches them.
    // Vulkan backend: writes values into a push-constant / UBO staging area.
    virtual void uniform_1f(const char* name, float v) = 0;
    virtual void uniform_2f(const char* name, float x, float y) = 0;
    virtual void uniform_4f(const char* name, float x, float y,
                             float z, float w) = 0;
    virtual void uniform_m4(const char* name,
                             const float* col_major_mat4) = 0;
    virtual void uniform_1i(const char* name, int v) = 0;
    virtual void uniform_2i(const char* name, int x, int y) = 0;

    // Bind a texture to the given unit and assign the sampler uniform on the
    // currently bound shader.
    // GL:     glActiveTexture + glBindTexture + glUniform1i(sampler_name, unit)
    // Vulkan: update the descriptor set binding for the current draw.
    virtual void bind_texture(int unit, TextureHandle,
                               const char* sampler_name) = 0;

    // Issue a triangle draw call.
    // Binds buf with the specified vertex layout, then draws count vertices
    // starting at first.
    virtual void draw_triangles(BufferHandle buf, const VertexLayout& layout,
                                 int first, int count) = 0;
};

} // namespace bl_ui::gfx
