#pragma once
#include "backend.h"

// ---------------------------------------------------------------------------
// VkBackend — Vulkan implementation of Backend.
//
// Build requirements:
//   - Vulkan SDK (VULKAN_SDK env var) — provides Vulkan headers/lib and glslang
//   - GLFW built with Vulkan support
//
// Shader differences vs GL backend:
//   Vulkan shaders must use:
//     • #version 450 (or 460)
//     • layout(push_constant) uniform PC { ... } pc;  for small uniforms
//     • layout(set=0, binding=0) uniform sampler2D uTex;  for textures
//   The GlBackend accepts the original #version 330 GLSL from the renderers.
//   For the VkBackend each renderer must supply Vulkan-compatible GLSL.
//   See src/render/gfx/README.md for the per-renderer shader mapping.
// ---------------------------------------------------------------------------

#ifdef BLUI_VULKAN_BACKEND

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <array>

struct GLFWwindow;

namespace bl_ui::gfx {

// Maximum number of frames in flight (double-buffered rendering).
static constexpr int VK_FRAMES_IN_FLIGHT = 2;

// Maximum push-constant block size (bytes). Vulkan guarantees at least 128.
static constexpr uint32_t VK_PUSH_CONSTANT_MAX = 128;

// Staging buffer capacity — must cover the largest possible uniform block.
// Shaders whose block exceeds VK_PUSH_CONSTANT_MAX fall back to the LUB
// (Large Uniform Buffer, 4096 bytes), so the staging area must match that.
static constexpr uint32_t VK_STAGING_MAX = 4096;

// ---------------------------------------------------------------------------
// Uniform staging area — shared by both push-constant and LUB shaders.
// uniform_*() accumulates values here; draw_triangles() either submits them
// via vkCmdPushConstants (small shaders) or memcpy-s them into the LUB.
// ---------------------------------------------------------------------------
struct StagingBlock {
    uint8_t  data[VK_STAGING_MAX] = {};
    uint32_t size = 0;  // bytes actually written
};

// ---------------------------------------------------------------------------
// Pipeline cache key: identifies a unique pipeline state.
// Different vertex layouts or shaders require separate VkPipeline objects.
// ---------------------------------------------------------------------------
struct PipelineKey {
    ShaderHandle shader;
    uint32_t     layout_hash;
    bool operator==(const PipelineKey& o) const noexcept {
        return shader == o.shader && layout_hash == o.layout_hash;
    }
};
struct PipelineKeyHash {
    size_t operator()(const PipelineKey& k) const noexcept {
        return std::hash<uint64_t>{}((uint64_t)k.shader << 32 | k.layout_hash);
    }
};

// ---------------------------------------------------------------------------
// VkBackend
// ---------------------------------------------------------------------------
class VkBackend final : public Backend {
public:
    explicit VkBackend(GLFWwindow* window);
    ~VkBackend() override;

    bool init(void* window, int fb_w, int fb_h) override;
    void shutdown() override;

    void begin_frame(int fb_w, int fb_h) override;
    void clear(float r, float g, float b, float a) override;
    void end_frame() override;

    ShaderHandle  create_shader (const char* vs_glsl,
                                  const char* fs_glsl) override;
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

    void use_shader    (ShaderHandle) override;
    void uniform_1f    (const char* name, float v) override;
    void uniform_2f    (const char* name, float x, float y) override;
    void uniform_4f    (const char* name, float x, float y,
                         float z, float w) override;
    void uniform_m4    (const char* name, const float* col_major) override;
    void uniform_1i    (const char* name, int v) override;
    void uniform_2i    (const char* name, int x, int y) override;
    void bind_texture  (int unit, TextureHandle,
                         const char* sampler_name) override;
    void draw_triangles(BufferHandle buf, const VertexLayout& layout,
                         int first, int count) override;

private:
    // -----------------------------------------------------------------------
    // Core Vulkan objects (created in init())
    // -----------------------------------------------------------------------
    GLFWwindow*       _window      = nullptr;
    VkInstance        _instance    = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT _debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR      _surface     = VK_NULL_HANDLE;
    VkPhysicalDevice  _phys_dev    = VK_NULL_HANDLE;
    VkDevice          _device      = VK_NULL_HANDLE;
    VkQueue           _gfx_queue   = VK_NULL_HANDLE;
    VkQueue           _present_queue = VK_NULL_HANDLE;
    uint32_t          _gfx_family    = 0;
    uint32_t          _present_family = 0;

    // -----------------------------------------------------------------------
    // Swapchain
    // -----------------------------------------------------------------------
    VkSwapchainKHR           _swapchain      = VK_NULL_HANDLE;
    VkFormat                 _sc_format      = VK_FORMAT_UNDEFINED;
    VkExtent2D               _sc_extent      = {};
    std::vector<VkImage>     _sc_images;
    std::vector<VkImageView> _sc_image_views;
    std::vector<VkFramebuffer> _framebuffers;

    // -----------------------------------------------------------------------
    // Render pass (single pass covering the full frame)
    // -----------------------------------------------------------------------
    VkRenderPass _render_pass = VK_NULL_HANDLE;

    // -----------------------------------------------------------------------
    // Command pool / buffers (one per frame-in-flight)
    // -----------------------------------------------------------------------
    VkCommandPool                              _cmd_pool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, VK_FRAMES_IN_FLIGHT> _cmd_bufs = {};

    // -----------------------------------------------------------------------
    // Synchronisation primitives
    // -----------------------------------------------------------------------
    std::array<VkSemaphore, VK_FRAMES_IN_FLIGHT> _img_available = {};
    std::array<VkSemaphore, VK_FRAMES_IN_FLIGHT> _render_done   = {};
    std::array<VkFence,     VK_FRAMES_IN_FLIGHT> _in_flight     = {};
    int      _frame_idx  = 0;   // current frame-in-flight slot
    uint32_t _image_idx  = 0;   // current swapchain image index

    // -----------------------------------------------------------------------
    // Descriptor infrastructure
    //   set 0 — combined-image-samplers (one per texture)
    //   set 1 — large uniform buffer (LUB) for shaders exceeding the 128-byte
    //           push-constant limit; double-buffered to match frames-in-flight
    // -----------------------------------------------------------------------
    VkDescriptorPool      _desc_pool      = VK_NULL_HANDLE;
    VkDescriptorSetLayout _tex_set_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _lub_set_layout = VK_NULL_HANDLE;

    static constexpr size_t LUB_SIZE = 4096;
    std::array<VkBuffer,      VK_FRAMES_IN_FLIGHT> _lub_buf    = {};
    std::array<VkDeviceMemory, VK_FRAMES_IN_FLIGHT> _lub_mem   = {};
    std::array<void*,         VK_FRAMES_IN_FLIGHT> _lub_mapped = {};
    std::array<VkDescriptorSet, VK_FRAMES_IN_FLIGHT> _lub_desc = {};

    // -----------------------------------------------------------------------
    // Pipeline layout — shared across all pipelines.
    // Push constants:  range [0, VK_PUSH_CONSTANT_MAX) for small uniforms.
    // Descriptor set 0: up to 256 combined-image-samplers for textures.
    // Descriptor set 1: large uniform buffer for oversized uniform blocks.
    // -----------------------------------------------------------------------
    VkPipelineLayout _pipeline_layout = VK_NULL_HANDLE;

    // -----------------------------------------------------------------------
    // Pipeline cache (lazy creation, one pipeline per shader+layout combo)
    // -----------------------------------------------------------------------
    VkPipelineCache _pipeline_cache = VK_NULL_HANDLE;
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> _pipelines;

    // -----------------------------------------------------------------------
    // Resource tables (ShaderHandle → Vulkan objects, etc.)
    // -----------------------------------------------------------------------
    struct UniformSlot { uint32_t offset; uint32_t size; };

    struct ShaderEntry {
        VkShaderModule vert_module;
        VkShaderModule frag_module;
        std::unordered_map<std::string, UniformSlot> uniform_map;
        bool uses_lub = false;  // true when uniform block exceeds VK_PUSH_CONSTANT_MAX
    };
    struct BufferEntry {
        VkBuffer       buffer;
        VkDeviceMemory memory;
        size_t         size;
    };
    struct TextureEntry {
        VkImage        image;
        VkDeviceMemory memory;
        VkImageView    view;
        VkSampler      sampler;
        VkDescriptorSet desc_set;   // bound texture descriptor
        PixelFormat    fmt;
    };

    uint32_t _next_id = 1;
    std::unordered_map<ShaderHandle,  ShaderEntry>  _shaders;
    std::unordered_map<BufferHandle,  BufferEntry>  _buffers;
    std::unordered_map<TextureHandle, TextureEntry> _textures;

    // -----------------------------------------------------------------------
    // Per-draw state (reset each use_shader() call)
    // -----------------------------------------------------------------------
    ShaderHandle  _cur_shader    = 0;
    bool          _blend_alpha   = true;
    bool          _frame_active  = false;  // true between a successful begin_frame and end_frame
    StagingBlock  _pc_staging;              // accumulated uniform values
    TextureHandle _bound_textures[8] = {};  // per unit


    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------
    bool _create_instance();
    bool _create_surface();
    bool _pick_physical_device();
    bool _create_device();
    bool _create_swapchain(int fb_w, int fb_h);
    bool _create_render_pass();
    bool _create_framebuffers();
    bool _create_command_pool_and_buffers();
    bool _create_sync_primitives();
    bool _create_descriptor_infrastructure();
    bool _create_lub();
    bool _create_pipeline_layout();

    void _destroy_swapchain();
    bool _recreate_swapchain(int fb_w, int fb_h);

    // Compile GLSL to SPIR-V using shaderc (runtime compilation).
    std::vector<uint32_t> _compile_glsl(const char* src,
                                         bool is_fragment) const;

    VkPipeline _get_or_create_pipeline(ShaderHandle,
                                        const VertexLayout&);

    // Memory helpers
    uint32_t _find_memory_type(uint32_t type_filter,
                                VkMemoryPropertyFlags props) const;
    bool _create_buffer_vk(size_t size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags props,
                            VkBuffer& buf, VkDeviceMemory& mem);
    bool _create_image_vk(int w, int h, VkFormat fmt,
                           VkImageUsageFlags usage,
                           VkImage& img, VkDeviceMemory& mem);

    // Single-use command buffer for upload transfers.
    VkCommandBuffer _begin_transfer_cmd();
    void            _end_transfer_cmd(VkCommandBuffer);

    void _transition_image_layout(VkCommandBuffer cmd, VkImage img,
                                   VkImageLayout from, VkImageLayout to);

    static uint32_t _layout_hash(const VertexLayout&) noexcept;

    // Look up the UniformSlot for `name` in the currently active shader.
    // Returns nullptr if no shader is active or the name is not found.
    const UniformSlot* _find_slot(const char* name) const;

    // Vulkan debug messenger callback.
    static VKAPI_ATTR VkBool32 VKAPI_CALL _debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT*,
        void*);
};

} // namespace bl_ui::gfx

#else  // !BLUI_VULKAN_BACKEND

// When Vulkan SDK is not available, provide a stub that errors at runtime.
namespace bl_ui::gfx {

class VkBackend final : public Backend {
public:
    explicit VkBackend(void* /*window*/) {}
    bool init(void* /*window*/, int /*fb_w*/, int /*fb_h*/) override {
        return false;
    }
    void shutdown() override {}
    void begin_frame(int, int) override {}
    void clear(float, float, float, float) override {}
    void end_frame() override {}
    ShaderHandle  create_shader (const char*, const char*) override { return 0; }
    BufferHandle  create_buffer (const void*, size_t, bool) override { return 0; }
    TextureHandle create_texture(int, int, PixelFormat, FilterMode,
                                  const void*) override { return 0; }
    void update_buffer (BufferHandle, const void*, size_t, size_t) override {}
    void update_texture(TextureHandle, int, int, int, int,
                         const void*) override {}
    void destroy_shader (ShaderHandle)  override {}
    void destroy_buffer (BufferHandle)  override {}
    void destroy_texture(TextureHandle) override {}
    void set_blend_alpha(bool) override {}
    void set_depth(bool, bool, bool) override {}
    void use_shader    (ShaderHandle) override {}
    void uniform_1f    (const char*, float) override {}
    void uniform_2f    (const char*, float, float) override {}
    void uniform_4f    (const char*, float, float, float, float) override {}
    void uniform_m4    (const char*, const float*) override {}
    void uniform_1i    (const char*, int) override {}
    void uniform_2i    (const char*, int, int) override {}
    void bind_texture  (int, TextureHandle, const char*) override {}
    void draw_triangles(BufferHandle, const VertexLayout&,
                         int, int) override {}
};

} // namespace bl_ui::gfx

#endif  // BLUI_VULKAN_BACKEND
