#include "vk_backend.h"

#ifdef BLUI_VULKAN_BACKEND

#include <GLFW/glfw3.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <set>
#include <algorithm>
#include <limits>

// glslang ships with the Vulkan SDK and compiles GLSL → SPIR-V.
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

// ---------------------------------------------------------------------------
// GL GLSL → Vulkan GLSL preprocessing helpers (anonymous namespace)
// ---------------------------------------------------------------------------
namespace {

struct ParsedUniform {
    std::string type;
    std::string name;
    bool     is_sampler = false;
    uint32_t size       = 4;
    uint32_t align      = 4;
};

static bool is_sampler_type(const std::string& t) {
    return t.rfind("sampler", 0) == 0 || t.rfind("image", 0) == 0;
}

struct TypeInfo { uint32_t size; uint32_t align; };
static TypeInfo glsl_type_info(const std::string& t) {
    if (t == "float" || t == "int" || t == "uint" || t == "bool") return {4,  4};
    if (t == "vec2"  || t == "ivec2" || t == "uvec2")             return {8,  8};
    if (t == "vec3"  || t == "ivec3" || t == "uvec3")             return {12, 16};
    if (t == "vec4"  || t == "ivec4" || t == "uvec4")             return {16, 16};
    if (t == "mat3")                                               return {48, 16};
    if (t == "mat4")                                               return {64, 16};
    return {4, 4};
}

// Parse uniform declarations from a GLSL source string.
// Skips names already in `seen` (for deduplicating VS/FS uniforms).
static void collect_uniforms(const char* src,
                              std::vector<ParsedUniform>& out,
                              std::set<std::string>& seen) {
    std::istringstream ss(src);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos) continue;
        std::string rest = line.substr(p);

        // Skip layout(...) prefix if present.
        if (rest.rfind("layout", 0) == 0) {
            size_t upos = rest.find("uniform");
            if (upos == std::string::npos) continue;
            rest = rest.substr(upos);
        }
        if (rest.rfind("uniform ", 0) != 0) continue;

        std::istringstream ls(rest);
        std::string kw, type, name;
        ls >> kw >> type >> name;
        while (!name.empty() && (name.back() == ';' || name.back() == '\r'))
            name.pop_back();
        // Strip array brackets (e.g. "uArr[4]" → "uArr")
        size_t bracket = name.find('[');
        if (bracket != std::string::npos) name = name.substr(0, bracket);

        if (name.empty() || seen.count(name)) continue;
        seen.insert(name);

        ParsedUniform u;
        u.type       = type;
        u.name       = name;
        u.is_sampler = is_sampler_type(type);
        if (!u.is_sampler) {
            auto ti = glsl_type_info(type);
            u.size  = ti.size;
            u.align = ti.align;
        }
        out.push_back(u);
    }
}

// {offset, size} pair — avoids accessing the private VkBackend::UniformSlot directly.
using OffsetMap = std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>;

// Compute std430 byte offsets for non-sampler uniforms.  Returns total block size.
static uint32_t assign_offsets(const std::vector<ParsedUniform>& uniforms,
                                OffsetMap& out_map) {
    uint32_t offset = 0;
    for (const auto& u : uniforms) {
        if (u.is_sampler) continue;
        offset = (offset + u.align - 1) & ~(u.align - 1);
        out_map[u.name] = { offset, u.size };
        offset += u.size;
    }
    return offset;
}

// Rewrite one GL GLSL shader stage into Vulkan-compatible GLSL.
//   - Replaces #version directive with "#version 450"
//   - Removes bare non-sampler uniform declarations (moved into the uniform block)
//   - Injects the push_constant / UBO block immediately after the version line
//   - Adds layout(set=0, binding=N) to sampler uniforms
//   - Auto-assigns layout(location=N) to in/out varyings that lack explicit locations
//     (Vulkan GLSL requires explicit location on all user in/out variables)
static std::string rewrite_glsl(const char* src,
                                 const std::string& block_glsl,
                                 int& sampler_binding) {
    std::istringstream ss(src);
    std::string line, out;
    bool injected = false;
    int in_loc = 0, out_loc = 0;  // auto-assigned location counters

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t p = line.find_first_not_of(" \t");
        std::string trimmed = (p != std::string::npos) ? line.substr(p) : "";

        // Version directive → replace and inject uniform block.
        if (trimmed.rfind("#version", 0) == 0) {
            out += "#version 450\n";
            if (!block_glsl.empty() && !injected) {
                out += block_glsl;
                injected = true;
            }
            continue;
        }

        // Isolate "uniform ..." prefix (possibly preceded by "layout(...)").
        std::string urest = trimmed;
        bool had_layout = (trimmed.rfind("layout", 0) == 0);
        if (had_layout) {
            size_t upos = urest.find("uniform");
            if (upos != std::string::npos) urest = urest.substr(upos);
        }

        if (urest.rfind("uniform ", 0) == 0) {
            std::istringstream ls(urest);
            std::string kw, type, name;
            ls >> kw >> type >> name;
            while (!name.empty() && (name.back() == ';' || name.back() == '\r'))
                name.pop_back();
            size_t bracket = name.find('[');
            if (bracket != std::string::npos) name = name.substr(0, bracket);

            if (is_sampler_type(type)) {
                if (!had_layout) {
                    out += "layout(set=0, binding=" + std::to_string(sampler_binding++) +
                           ") uniform " + type + " " + name + ";\n";
                } else {
                    out += line + "\n";
                }
            }
            // Non-sampler: already in push_constant / UBO block — skip.
            continue;
        }

        // Auto-assign location to in/out varyings that have no explicit layout.
        // Vertex attribute inputs already have layout(location=N) so they're
        // captured by the `had_layout` path above and kept verbatim.
        if (!had_layout) {
            std::istringstream ls(trimmed);
            std::string first, type, name;
            ls >> first >> type >> name;
            while (!name.empty() && (name.back() == ';' || name.back() == '\r'))
                name.pop_back();
            if (!name.empty() && (first == "in" || first == "out")) {
                int& loc = (first == "in") ? in_loc : out_loc;
                out += "layout(location = " + std::to_string(loc++) +
                       ") " + first + " " + type + " " + name + ";\n";
                continue;
            }
        }

        out += line + "\n";
    }

    // Fallback if source had no #version (shouldn't happen).
    if (!injected && !block_glsl.empty())
        out = "#version 450\n" + block_glsl + out;

    return out;
}

// Build the uniform block GLSL string.
// If uses_lub: generates a std430 UBO at set=1,binding=0 for large uniform blocks.
// Otherwise: generates a push_constant block (≤128 bytes guaranteed).
static std::string build_uniform_block(const std::vector<ParsedUniform>& uniforms,
                                        const OffsetMap& offsets,
                                        bool uses_lub) {
    bool any = false;
    for (const auto& u : uniforms) if (!u.is_sampler) { any = true; break; }
    if (!any) return {};

    std::string block;
    if (uses_lub)
        block = "layout(std140, set=1, binding=0) uniform _UB {\n";
    else
        block = "layout(push_constant) uniform _PC {\n";

    for (const auto& u : uniforms) {
        if (u.is_sampler) continue;
        auto it = offsets.find(u.name);
        if (it == offsets.end()) continue;
        block += "    layout(offset = " + std::to_string(it->second.first) +
                 ") " + u.type + " " + u.name + ";\n";
    }
    block += "};\n";
    return block;
}

} // anonymous namespace

namespace bl_ui::gfx {

// ---------------------------------------------------------------------------
// Validation layers (debug builds only)
// ---------------------------------------------------------------------------
#ifdef NDEBUG
static constexpr bool VALIDATION_ENABLED = false;
#else
static constexpr bool VALIDATION_ENABLED = true;
#endif

static const std::vector<const char*> VALIDATION_LAYERS = {
    "VK_LAYER_KHRONOS_validation",
};

static const std::vector<const char*> DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

VkBackend::VkBackend(GLFWwindow* window) : _window(window) {}

VkBackend::~VkBackend() { shutdown(); }

// ---------------------------------------------------------------------------
// init — create all core Vulkan objects
// ---------------------------------------------------------------------------

bool VkBackend::init(void* window, int fb_w, int fb_h) {
    if (window) _window = static_cast<GLFWwindow*>(window);

    if (!glslang_initialize_process()) {
        std::cerr << "[VkBackend] glslang_initialize_process failed\n";
        return false;
    }

    if (!_create_instance())                  return false;
    if (!_create_surface())                   return false;
    if (!_pick_physical_device())             return false;
    if (!_create_device())                    return false;
    if (!_create_swapchain(fb_w, fb_h))       return false;
    if (!_create_render_pass())               return false;
    if (!_create_framebuffers())              return false;
    if (!_create_command_pool_and_buffers())  return false;
    if (!_create_sync_primitives())           return false;
    if (!_create_descriptor_infrastructure()) return false;
    if (!_create_lub())                       return false;
    if (!_create_pipeline_layout())           return false;
    VkPipelineCacheCreateInfo pc_cache_ci{};
    pc_cache_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    if (vkCreatePipelineCache(_device, &pc_cache_ci, nullptr, &_pipeline_cache) != VK_SUCCESS)
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void VkBackend::shutdown() {
    if (!_device) return;
    vkDeviceWaitIdle(_device);
    glslang_finalize_process();

    for (auto& [k, p] : _pipelines) vkDestroyPipeline(_device, p, nullptr);
    _pipelines.clear();

    if (_pipeline_cache)   vkDestroyPipelineCache(_device, _pipeline_cache, nullptr);
    if (_pipeline_layout)  vkDestroyPipelineLayout(_device, _pipeline_layout, nullptr);
    if (_desc_pool)        vkDestroyDescriptorPool(_device, _desc_pool, nullptr);
    if (_tex_set_layout)   vkDestroyDescriptorSetLayout(_device, _tex_set_layout, nullptr);
    if (_lub_set_layout)   vkDestroyDescriptorSetLayout(_device, _lub_set_layout, nullptr);
    for (int i = 0; i < VK_FRAMES_IN_FLIGHT; ++i) {
        if (_lub_mapped[i]) vkUnmapMemory(_device, _lub_mem[i]);
        if (_lub_buf[i])    vkDestroyBuffer(_device, _lub_buf[i], nullptr);
        if (_lub_mem[i])    vkFreeMemory(_device, _lub_mem[i], nullptr);
    }

    for (auto& s : _img_available) if (s) vkDestroySemaphore(_device, s, nullptr);
    for (auto& s : _render_done)   if (s) vkDestroySemaphore(_device, s, nullptr);
    for (auto& f : _in_flight)     if (f) vkDestroyFence(_device, f, nullptr);

    if (_cmd_pool) vkDestroyCommandPool(_device, _cmd_pool, nullptr);

    _destroy_swapchain();
    if (_render_pass) vkDestroyRenderPass(_device, _render_pass, nullptr);

    // Destroy all remaining resources
    for (auto& [id, e] : _shaders) {
        vkDestroyShaderModule(_device, e.vert_module, nullptr);
        vkDestroyShaderModule(_device, e.frag_module, nullptr);
    }
    for (auto& [id, e] : _buffers) {
        vkDestroyBuffer(_device, e.buffer, nullptr);
        vkFreeMemory(_device, e.memory, nullptr);
    }
    for (auto& [id, e] : _textures) {
        vkDestroyImageView(_device, e.view, nullptr);
        vkDestroyImage(_device, e.image, nullptr);
        vkFreeMemory(_device, e.memory, nullptr);
        vkDestroySampler(_device, e.sampler, nullptr);
    }

    vkDestroyDevice(_device, nullptr);
    if (_debug_messenger) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(_instance, _debug_messenger, nullptr);
    }
    if (_surface)  vkDestroySurfaceKHR(_instance, _surface, nullptr);
    if (_instance) vkDestroyInstance(_instance, nullptr);
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void VkBackend::begin_frame(int fb_w, int fb_h) {
    _frame_active = false;

    // Wait for the previous use of this frame slot to finish.
    vkWaitForFences(_device, 1, &_in_flight[_frame_idx], VK_TRUE, UINT64_MAX);

    // Acquire the next swapchain image.
    VkResult result = vkAcquireNextImageKHR(
        _device, _swapchain, UINT64_MAX,
        _img_available[_frame_idx], VK_NULL_HANDLE, &_image_idx);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Don't reset the fence — it's still signaled, so next begin_frame
        // for this slot will return from vkWaitForFences immediately.
        _recreate_swapchain(fb_w, fb_h);
        return;
    }
    if (result != VK_SUCCESS) return;

    vkResetFences(_device, 1, &_in_flight[_frame_idx]);

    // Begin recording the command buffer for this frame.
    VkCommandBuffer cmd = _cmd_bufs[_frame_idx];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin_info);

    _frame_active = true;
}

void VkBackend::clear(float r, float g, float b, float a) {
    if (!_frame_active) return;
    VkCommandBuffer cmd = _cmd_bufs[_frame_idx];

    VkClearValue clear_val{};
    clear_val.color = {{ r, g, b, a }};

    VkRenderPassBeginInfo rp_info{};
    rp_info.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass        = _render_pass;
    rp_info.framebuffer       = _framebuffers[_image_idx];
    rp_info.renderArea.offset = { 0, 0 };
    rp_info.renderArea.extent = _sc_extent;
    rp_info.clearValueCount   = 1;
    rp_info.pClearValues      = &clear_val;

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    // Flip Y so NDC +1 = screen top, matching the OpenGL convention used by
    // all shaders (ny = -(screen_y / h)*2 + 1).  Negative height is core
    // in Vulkan 1.1+ (VK_KHR_maintenance1).
    VkViewport vp{ 0.f, (float)_sc_extent.height,
                   (float)_sc_extent.width, -(float)_sc_extent.height,
                   0.f, 1.f };
    VkRect2D   sc{ {0, 0}, _sc_extent };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor (cmd, 0, 1, &sc);
}

void VkBackend::end_frame() {
    if (!_frame_active) return;
    _frame_active = false;

    VkCommandBuffer cmd = _cmd_bufs[_frame_idx];
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Submit the command buffer.
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &_img_available[_frame_idx];
    submit.pWaitDstStageMask    = &wait_stage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &_render_done[_frame_idx];

    vkQueueSubmit(_gfx_queue, 1, &submit, _in_flight[_frame_idx]);

    // Present.
    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &_render_done[_frame_idx];
    present.swapchainCount     = 1;
    present.pSwapchains        = &_swapchain;
    present.pImageIndices      = &_image_idx;

    vkQueuePresentKHR(_present_queue, &present);
    _frame_idx = (_frame_idx + 1) % VK_FRAMES_IN_FLIGHT;
}

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------

ShaderHandle VkBackend::create_shader(const char* vs_glsl,
                                       const char* fs_glsl) {
    // Step 1: parse all uniforms from both stages (VS first, then FS extras).
    std::vector<ParsedUniform> uniforms;
    std::set<std::string> seen;
    collect_uniforms(vs_glsl, uniforms, seen);
    collect_uniforms(fs_glsl, uniforms, seen);

    // Step 2: compute std430 offsets into a local map (avoids private type access),
    // then convert to the per-shader UniformSlot map.
    OffsetMap offsets;
    assign_offsets(uniforms, offsets);

    // Shaders whose total uniform block exceeds VK_PUSH_CONSTANT_MAX use a
    // large uniform buffer (LUB at set=1) instead of push constants.
    uint32_t block_size = 0;
    for (const auto& [n, p] : offsets) block_size = std::max(block_size, p.first + p.second);
    bool uses_lub = (block_size > VK_PUSH_CONSTANT_MAX);

    ShaderEntry entry{};
    entry.uses_lub = uses_lub;
    for (const auto& [name, p] : offsets)
        entry.uniform_map[name] = { p.first, p.second };

    // Step 3: build the uniform block GLSL (push_constant or UBO).
    std::string block_glsl = build_uniform_block(uniforms, offsets, uses_lub);

    // Step 4: rewrite both stages to Vulkan-compatible GLSL.
    int sampler_binding = 0;
    std::string vk_vs = rewrite_glsl(vs_glsl, block_glsl, sampler_binding);
    sampler_binding = 0;
    std::string vk_fs = rewrite_glsl(fs_glsl, block_glsl, sampler_binding);

    // Step 5: compile Vulkan GLSL → SPIR-V.
    auto vert_spv = _compile_glsl(vk_vs.c_str(), false);
    auto frag_spv = _compile_glsl(vk_fs.c_str(), true);
    if (vert_spv.empty() || frag_spv.empty()) return 0;

    auto make_module = [&](const std::vector<uint32_t>& spv) -> VkShaderModule {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = spv.size() * 4;
        ci.pCode    = spv.data();
        VkShaderModule mod = VK_NULL_HANDLE;
        vkCreateShaderModule(_device, &ci, nullptr, &mod);
        return mod;
    };

    entry.vert_module = make_module(vert_spv);
    entry.frag_module = make_module(frag_spv);
    if (!entry.vert_module || !entry.frag_module) return 0;

    uint32_t id = _next_id++;
    _shaders[id] = std::move(entry);
    return id;
}

BufferHandle VkBackend::create_buffer(const void* data, size_t bytes,
                                       bool dynamic) {
    // Dynamic buffers are host-visible for direct CPU writes.
    // Static buffers use a staging buffer + device-local memory for performance.
    VkMemoryPropertyFlags props = dynamic
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                             | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    BufferEntry entry{};
    entry.size = bytes;
    if (!_create_buffer_vk(bytes, usage, props, entry.buffer, entry.memory))
        return 0;

    if (data) {
        if (dynamic) {
            // Host-visible: map and copy directly.
            void* mapped;
            vkMapMemory(_device, entry.memory, 0, bytes, 0, &mapped);
            memcpy(mapped, data, bytes);
            vkUnmapMemory(_device, entry.memory);
        } else {
            // Device-local: use a staging buffer.
            VkBuffer staging_buf; VkDeviceMemory staging_mem;
            _create_buffer_vk(bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                staging_buf, staging_mem);

            void* mapped;
            vkMapMemory(_device, staging_mem, 0, bytes, 0, &mapped);
            memcpy(mapped, data, bytes);
            vkUnmapMemory(_device, staging_mem);

            // Copy staging → device via transfer command buffer.
            VkCommandBuffer cmd = _begin_transfer_cmd();
            VkBufferCopy region{ 0, 0, bytes };
            vkCmdCopyBuffer(cmd, staging_buf, entry.buffer, 1, &region);
            _end_transfer_cmd(cmd);

            vkDestroyBuffer(_device, staging_buf, nullptr);
            vkFreeMemory(_device, staging_mem, nullptr);
        }
    }

    uint32_t id = _next_id++;
    _buffers[id] = entry;
    return id;
}

TextureHandle VkBackend::create_texture(int w, int h, PixelFormat fmt,
                                         FilterMode filter,
                                         const void* pixels) {
    VkFormat vk_fmt = (fmt == PixelFormat::R8) ? VK_FORMAT_R8_UNORM
                                                : VK_FORMAT_R8G8B8A8_UNORM;
    int channels = (fmt == PixelFormat::R8) ? 1 : 4;

    TextureEntry entry{};
    entry.fmt = fmt;

    // Create the device-local VkImage.
    if (!_create_image_vk(w, h, vk_fmt,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            entry.image, entry.memory))
        return 0;

    // Upload initial data via staging buffer.
    if (pixels) {
        size_t nbytes = (size_t)w * h * channels;
        VkBuffer staging_buf; VkDeviceMemory staging_mem;
        _create_buffer_vk(nbytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging_buf, staging_mem);

        void* mapped;
        vkMapMemory(_device, staging_mem, 0, nbytes, 0, &mapped);
        memcpy(mapped, pixels, nbytes);
        vkUnmapMemory(_device, staging_mem);

        VkCommandBuffer cmd = _begin_transfer_cmd();
        _transition_image_layout(cmd, entry.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
        vkCmdCopyBufferToImage(cmd, staging_buf, entry.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        _transition_image_layout(cmd, entry.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        _end_transfer_cmd(cmd);

        vkDestroyBuffer(_device, staging_buf, nullptr);
        vkFreeMemory(_device, staging_mem, nullptr);
    }

    // Image view.
    VkImageViewCreateInfo view_ci{};
    view_ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image                           = entry.image;
    view_ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format                          = vk_fmt;
    view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount     = 1;
    view_ci.subresourceRange.layerCount     = 1;
    vkCreateImageView(_device, &view_ci, nullptr, &entry.view);

    // Sampler.
    VkFilter vk_filter = (filter == FilterMode::Nearest) ? VK_FILTER_NEAREST
                                                          : VK_FILTER_LINEAR;
    VkSamplerCreateInfo samp_ci{};
    samp_ci.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp_ci.magFilter     = vk_filter;
    samp_ci.minFilter     = vk_filter;
    samp_ci.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_ci.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_ci.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    vkCreateSampler(_device, &samp_ci, nullptr, &entry.sampler);

    // Allocate and write a descriptor set for this texture.
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = _desc_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &_tex_set_layout;
    vkAllocateDescriptorSets(_device, &alloc_info, &entry.desc_set);

    VkDescriptorImageInfo img_info{};
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_info.imageView   = entry.view;
    img_info.sampler     = entry.sampler;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = entry.desc_set;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &img_info;
    vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);

    uint32_t id = _next_id++;
    _textures[id] = entry;
    return id;
}

// ---------------------------------------------------------------------------
// Resource update
// ---------------------------------------------------------------------------

void VkBackend::update_buffer(BufferHandle buf, const void* data,
                               size_t bytes, size_t offset) {
    auto it = _buffers.find(buf);
    if (it == _buffers.end()) return;

    // Assuming host-visible (dynamic) buffer — map and memcpy.
    // For device-local buffers a staging copy would be needed.
    void* mapped;
    vkMapMemory(_device, it->second.memory, offset, bytes, 0, &mapped);
    memcpy(mapped, data, bytes);
    vkUnmapMemory(_device, it->second.memory);
}

void VkBackend::update_texture(TextureHandle handle,
                                int x, int y, int w, int h,
                                const void* pixels) {
    auto it = _textures.find(handle);
    if (it == _textures.end()) return;
    int channels = (it->second.fmt == PixelFormat::R8) ? 1 : 4;
    size_t nbytes = (size_t)w * h * channels;

    VkBuffer staging_buf; VkDeviceMemory staging_mem;
    _create_buffer_vk(nbytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_buf, staging_mem);

    void* mapped;
    vkMapMemory(_device, staging_mem, 0, nbytes, 0, &mapped);
    memcpy(mapped, pixels, nbytes);
    vkUnmapMemory(_device, staging_mem);

    VkCommandBuffer cmd = _begin_transfer_cmd();
    _transition_image_layout(cmd, it->second.image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageOffset = { x, y, 0 };
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyBufferToImage(cmd, staging_buf, it->second.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    _transition_image_layout(cmd, it->second.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    _end_transfer_cmd(cmd);

    vkDestroyBuffer(_device, staging_buf, nullptr);
    vkFreeMemory(_device, staging_mem, nullptr);
}

// ---------------------------------------------------------------------------
// Resource destruction
// ---------------------------------------------------------------------------

void VkBackend::destroy_shader(ShaderHandle id) {
    auto it = _shaders.find(id);
    if (it == _shaders.end()) return;
    vkDestroyShaderModule(_device, it->second.vert_module, nullptr);
    vkDestroyShaderModule(_device, it->second.frag_module, nullptr);
    _shaders.erase(it);
    // Destroy any cached pipelines for this shader.
    for (auto pit = _pipelines.begin(); pit != _pipelines.end(); ) {
        if (pit->first.shader == id) {
            vkDestroyPipeline(_device, pit->second, nullptr);
            pit = _pipelines.erase(pit);
        } else ++pit;
    }
}

void VkBackend::destroy_buffer(BufferHandle id) {
    auto it = _buffers.find(id);
    if (it == _buffers.end()) return;
    vkDestroyBuffer(_device, it->second.buffer, nullptr);
    vkFreeMemory(_device, it->second.memory, nullptr);
    _buffers.erase(it);
}

void VkBackend::destroy_texture(TextureHandle id) {
    auto it = _textures.find(id);
    if (it == _textures.end()) return;
    vkDestroyImageView(_device, it->second.view, nullptr);
    vkDestroyImage(_device, it->second.image, nullptr);
    vkFreeMemory(_device, it->second.memory, nullptr);
    vkDestroySampler(_device, it->second.sampler, nullptr);
    _textures.erase(it);
}

// ---------------------------------------------------------------------------
// Rendering state
// ---------------------------------------------------------------------------

void VkBackend::set_blend_alpha(bool enabled) {
    // Blend mode is baked into the VkPipeline at creation.
    // The _blend_alpha flag is consulted during _get_or_create_pipeline().
    _blend_alpha = enabled;
}

void VkBackend::set_depth(bool /*test*/, bool /*write*/, bool /*always_pass*/) {
    // Depth state is baked into the VkPipeline. Tracked via pipeline key
    // (not yet implemented in the pipeline key — extend PipelineKey as needed).
}

// ---------------------------------------------------------------------------
// Shader / uniforms
// ---------------------------------------------------------------------------

void VkBackend::use_shader(ShaderHandle id) {
    _cur_shader = id;
    memset(_pc_staging.data, 0, VK_STAGING_MAX);
    _pc_staging.size = 0;
}

const VkBackend::UniformSlot* VkBackend::_find_slot(const char* name) const {
    auto sit = _shaders.find(_cur_shader);
    if (sit == _shaders.end()) return nullptr;
    auto it = sit->second.uniform_map.find(name);
    if (it == sit->second.uniform_map.end()) return nullptr;
    return &it->second;
}

static void write_pc(uint8_t* block, uint32_t offset, uint32_t size,
                      const void* src) {
    memcpy(block + offset, src, size);
}

void VkBackend::uniform_1f(const char* name, float v) {
    const auto* s = _find_slot(name); if (!s) return;
    write_pc(_pc_staging.data, s->offset, s->size, &v);
    _pc_staging.size = std::max(_pc_staging.size, s->offset + 4u);
}
void VkBackend::uniform_2f(const char* name, float x, float y) {
    const auto* s = _find_slot(name); if (!s) return;
    float v[2] = { x, y };
    write_pc(_pc_staging.data, s->offset, s->size, v);
    _pc_staging.size = std::max(_pc_staging.size, s->offset + 8u);
}
void VkBackend::uniform_4f(const char* name, float x, float y, float z, float w) {
    const auto* s = _find_slot(name); if (!s) return;
    float v[4] = { x, y, z, w };
    write_pc(_pc_staging.data, s->offset, s->size, v);
    _pc_staging.size = std::max(_pc_staging.size, s->offset + 16u);
}
void VkBackend::uniform_m4(const char* name, const float* m) {
    const auto* s = _find_slot(name); if (!s) return;
    write_pc(_pc_staging.data, s->offset, s->size, m);
    _pc_staging.size = std::max(_pc_staging.size, s->offset + 64u);
}
void VkBackend::uniform_1i(const char* name, int v) {
    const auto* s = _find_slot(name); if (!s) return;
    write_pc(_pc_staging.data, s->offset, s->size, &v);
    _pc_staging.size = std::max(_pc_staging.size, s->offset + 4u);
}
void VkBackend::uniform_2i(const char* name, int x, int y) {
    const auto* s = _find_slot(name); if (!s) return;
    int v[2] = { x, y };
    write_pc(_pc_staging.data, s->offset, s->size, v);
    _pc_staging.size = std::max(_pc_staging.size, s->offset + 8u);
}

void VkBackend::bind_texture(int unit, TextureHandle handle,
                               const char* /*sampler_name*/) {
    if (unit >= 0 && unit < 8) _bound_textures[unit] = handle;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void VkBackend::draw_triangles(BufferHandle buf, const VertexLayout& layout,
                                int first, int count) {
    if (!_frame_active) return;
    auto bit = _buffers.find(buf);
    if (bit == _buffers.end()) return;

    VkCommandBuffer cmd = _cmd_bufs[_frame_idx];

    // Bind (or lazily create) the pipeline for this shader + vertex layout.
    VkPipeline pipeline = _get_or_create_pipeline(_cur_shader, layout);
    if (!pipeline) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    auto sit = _shaders.find(_cur_shader);
    bool uses_lub = (sit != _shaders.end() && sit->second.uses_lub);

    if (uses_lub) {
        // Flush staging data into the current frame's large uniform buffer.
        if (_pc_staging.size > 0)
            memcpy(_lub_mapped[_frame_idx], _pc_staging.data, _pc_staging.size);
    } else {
        // Push uniform data as push constants.
        if (_pc_staging.size > 0) {
            vkCmdPushConstants(cmd, _pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, _pc_staging.size, _pc_staging.data);
        }
    }

    // Always bind the LUB at set=1 (required by pipeline layout even if unused).
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        _pipeline_layout, 1, 1, &_lub_desc[_frame_idx], 0, nullptr);

    // Bind the first occupied texture descriptor set at set=0.
    for (int u = 0; u < 8; ++u) {
        if (_bound_textures[u]) {
            auto tit = _textures.find(_bound_textures[u]);
            if (tit != _textures.end()) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipeline_layout, 0, 1, &tit->second.desc_set, 0, nullptr);
            }
            break;
        }
    }

    // Bind vertex buffer and draw.
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &bit->second.buffer, &offset);
    vkCmdDraw(cmd, (uint32_t)count, 1, (uint32_t)first, 0);
}

// ---------------------------------------------------------------------------
// Vulkan initialisation helpers
// ---------------------------------------------------------------------------

bool VkBackend::_create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "bl_ui";
    app_info.apiVersion       = VK_API_VERSION_1_2;

    // Gather GLFW required extensions.
    uint32_t glfw_count = 0;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_count);
    std::vector<const char*> extensions(glfw_exts, glfw_exts + glfw_count);

    if (VALIDATION_ENABLED)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app_info;
    ci.enabledExtensionCount   = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();

    if (VALIDATION_ENABLED) {
        ci.enabledLayerCount   = (uint32_t)VALIDATION_LAYERS.size();
        ci.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    }

    if (vkCreateInstance(&ci, nullptr, &_instance) != VK_SUCCESS) {
        std::cerr << "[VkBackend] vkCreateInstance failed\n";
        return false;
    }

    // Set up debug messenger.
    if (VALIDATION_ENABLED) {
        VkDebugUtilsMessengerCreateInfoEXT dm_ci{};
        dm_ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dm_ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dm_ci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        dm_ci.pfnUserCallback = _debug_callback;

        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(_instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(_instance, &dm_ci, nullptr, &_debug_messenger);
    }

    return true;
}

bool VkBackend::_create_surface() {
    return glfwCreateWindowSurface(_instance, _window,
                                   nullptr, &_surface) == VK_SUCCESS;
}

bool VkBackend::_pick_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(_instance, &count, nullptr);
    if (!count) { std::cerr << "[VkBackend] No Vulkan-capable GPU\n"; return false; }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(_instance, &count, devices.data());

    // Prefer discrete GPU.
    for (auto dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            _phys_dev = dev;
            return true;
        }
    }
    _phys_dev = devices[0];
    return true;
}

bool VkBackend::_create_device() {
    uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(_phys_dev, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> qfams(qcount);
    vkGetPhysicalDeviceQueueFamilyProperties(_phys_dev, &qcount, qfams.data());

    bool found_gfx = false, found_present = false;
    for (uint32_t i = 0; i < qcount; ++i) {
        if (!found_gfx && (qfams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            _gfx_family = i; found_gfx = true;
        }
        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(_phys_dev, i, _surface, &present_support);
        if (!found_present && present_support) {
            _present_family = i; found_present = true;
        }
    }
    if (!found_gfx || !found_present) return false;

    std::set<uint32_t> unique_families{ _gfx_family, _present_family };
    float prio = 1.f;
    std::vector<VkDeviceQueueCreateInfo> queue_cis;
    for (auto fam : unique_families) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = fam;
        qci.queueCount       = 1;
        qci.pQueuePriorities = &prio;
        queue_cis.push_back(qci);
    }

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = (uint32_t)queue_cis.size();
    ci.pQueueCreateInfos       = queue_cis.data();
    ci.enabledExtensionCount   = (uint32_t)DEVICE_EXTENSIONS.size();
    ci.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
    ci.pEnabledFeatures        = &features;

    if (VALIDATION_ENABLED) {
        ci.enabledLayerCount   = (uint32_t)VALIDATION_LAYERS.size();
        ci.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    }

    if (vkCreateDevice(_phys_dev, &ci, nullptr, &_device) != VK_SUCCESS)
        return false;

    vkGetDeviceQueue(_device, _gfx_family,     0, &_gfx_queue);
    vkGetDeviceQueue(_device, _present_family,  0, &_present_queue);
    return true;
}

bool VkBackend::_create_swapchain(int fb_w, int fb_h) {
    // Query swapchain support.
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_phys_dev, _surface, &caps);

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(_phys_dev, _surface, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(_phys_dev, _surface, &fmt_count, formats.data());

    // Prefer UNORM swapchain format to match OpenGL behaviour.
    // Shader outputs are already in sRGB space (UI theme colours are sRGB constants),
    // so we must NOT use a _SRGB swapchain format — it would apply a second
    // linear→sRGB conversion, pushing all values toward 1.0 (washed-out image).
    VkSurfaceFormatKHR chosen = formats[0];
    for (auto& f : formats)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM ||
             f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            { chosen = f; break; }
    _sc_format = chosen.format;

    // Compute extent.
    if (caps.currentExtent.width != UINT32_MAX) {
        _sc_extent = caps.currentExtent;
    } else {
        _sc_extent.width  = std::clamp((uint32_t)fb_w,
                                       caps.minImageExtent.width,
                                       caps.maxImageExtent.width);
        _sc_extent.height = std::clamp((uint32_t)fb_h,
                                       caps.minImageExtent.height,
                                       caps.maxImageExtent.height);
    }

    uint32_t img_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) img_count = std::min(img_count, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = _surface;
    ci.minImageCount    = img_count;
    ci.imageFormat      = chosen.format;
    ci.imageColorSpace  = chosen.colorSpace;
    ci.imageExtent      = _sc_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;  // vsync
    ci.clipped          = VK_TRUE;

    uint32_t families[] = { _gfx_family, _present_family };
    if (_gfx_family != _present_family) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(_device, &ci, nullptr, &_swapchain) != VK_SUCCESS)
        return false;

    vkGetSwapchainImagesKHR(_device, _swapchain, &img_count, nullptr);
    _sc_images.resize(img_count);
    vkGetSwapchainImagesKHR(_device, _swapchain, &img_count, _sc_images.data());

    _sc_image_views.resize(img_count);
    for (uint32_t i = 0; i < img_count; ++i) {
        VkImageViewCreateInfo view_ci{};
        view_ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image                           = _sc_images[i];
        view_ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format                          = _sc_format;
        view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.levelCount     = 1;
        view_ci.subresourceRange.layerCount     = 1;
        vkCreateImageView(_device, &view_ci, nullptr, &_sc_image_views[i]);
    }
    return true;
}

bool VkBackend::_create_render_pass() {
    VkAttachmentDescription color_att{};
    color_att.format         = _sc_format;
    color_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    color_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color_att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_ci{};
    rp_ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments    = &color_att;
    rp_ci.subpassCount    = 1;
    rp_ci.pSubpasses      = &subpass;
    rp_ci.dependencyCount = 1;
    rp_ci.pDependencies   = &dep;

    return vkCreateRenderPass(_device, &rp_ci, nullptr, &_render_pass) == VK_SUCCESS;
}

bool VkBackend::_create_framebuffers() {
    _framebuffers.resize(_sc_image_views.size());
    for (size_t i = 0; i < _sc_image_views.size(); ++i) {
        VkFramebufferCreateInfo fb_ci{};
        fb_ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_ci.renderPass      = _render_pass;
        fb_ci.attachmentCount = 1;
        fb_ci.pAttachments    = &_sc_image_views[i];
        fb_ci.width           = _sc_extent.width;
        fb_ci.height          = _sc_extent.height;
        fb_ci.layers          = 1;
        if (vkCreateFramebuffer(_device, &fb_ci, nullptr, &_framebuffers[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

bool VkBackend::_create_command_pool_and_buffers() {
    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.queueFamilyIndex = _gfx_family;
    pool_ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(_device, &pool_ci, nullptr, &_cmd_pool) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = _cmd_pool;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = VK_FRAMES_IN_FLIGHT;
    return vkAllocateCommandBuffers(_device, &alloc_info,
                                    _cmd_bufs.data()) == VK_SUCCESS;
}

bool VkBackend::_create_sync_primitives() {
    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence_ci{};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < VK_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(_device, &sem_ci, nullptr, &_img_available[i]) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(_device, &sem_ci, nullptr, &_render_done[i])   != VK_SUCCESS) return false;
        if (vkCreateFence    (_device, &fence_ci, nullptr, &_in_flight[i])   != VK_SUCCESS) return false;
    }
    return true;
}

bool VkBackend::_create_descriptor_infrastructure() {
    // Set 0: combined image sampler for textures.
    VkDescriptorSetLayoutBinding tex_binding{};
    tex_binding.binding         = 0;
    tex_binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    tex_binding.descriptorCount = 1;
    tex_binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo tex_layout_ci{};
    tex_layout_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    tex_layout_ci.bindingCount = 1;
    tex_layout_ci.pBindings    = &tex_binding;
    if (vkCreateDescriptorSetLayout(_device, &tex_layout_ci,
                                    nullptr, &_tex_set_layout) != VK_SUCCESS)
        return false;

    // Set 1: large uniform buffer for shaders exceeding the push-constant limit.
    VkDescriptorSetLayoutBinding lub_binding{};
    lub_binding.binding         = 0;
    lub_binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lub_binding.descriptorCount = 1;
    lub_binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo lub_layout_ci{};
    lub_layout_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lub_layout_ci.bindingCount = 1;
    lub_layout_ci.pBindings    = &lub_binding;
    if (vkCreateDescriptorSetLayout(_device, &lub_layout_ci,
                                    nullptr, &_lub_set_layout) != VK_SUCCESS)
        return false;

    // Pool: 256 combined-image-sampler + VK_FRAMES_IN_FLIGHT uniform-buffer slots.
    VkDescriptorPoolSize pool_sizes[2] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_FRAMES_IN_FLIGHT },
    };
    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.maxSets       = 256 + VK_FRAMES_IN_FLIGHT;
    pool_ci.poolSizeCount = 2;
    pool_ci.pPoolSizes    = pool_sizes;
    return vkCreateDescriptorPool(_device, &pool_ci,
                                  nullptr, &_desc_pool) == VK_SUCCESS;
}

bool VkBackend::_create_lub() {
    for (int i = 0; i < VK_FRAMES_IN_FLIGHT; ++i) {
        if (!_create_buffer_vk(LUB_SIZE,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                _lub_buf[i], _lub_mem[i]))
            return false;
        vkMapMemory(_device, _lub_mem[i], 0, LUB_SIZE, 0, &_lub_mapped[i]);

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool     = _desc_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts        = &_lub_set_layout;
        vkAllocateDescriptorSets(_device, &alloc_info, &_lub_desc[i]);

        VkDescriptorBufferInfo buf_info{};
        buf_info.buffer = _lub_buf[i];
        buf_info.offset = 0;
        buf_info.range  = LUB_SIZE;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _lub_desc[i];
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo     = &buf_info;
        vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
    }
    return true;
}

bool VkBackend::_create_pipeline_layout() {
    // Push-constant range: small uniforms (push_constant shaders).
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset     = 0;
    pc_range.size       = VK_PUSH_CONSTANT_MAX;

    // Two descriptor sets: set=0 textures, set=1 large uniform buffer.
    VkDescriptorSetLayout set_layouts[2] = { _tex_set_layout, _lub_set_layout };

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = 2;
    ci.pSetLayouts            = set_layouts;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges    = &pc_range;
    return vkCreatePipelineLayout(_device, &ci,
                                  nullptr, &_pipeline_layout) == VK_SUCCESS;
}

void VkBackend::_destroy_swapchain() {
    for (auto fb : _framebuffers)    vkDestroyFramebuffer(_device, fb, nullptr);
    for (auto iv : _sc_image_views)  vkDestroyImageView(_device, iv, nullptr);
    if (_swapchain) vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    _framebuffers.clear();
    _sc_image_views.clear();
    _sc_images.clear();
    _swapchain = VK_NULL_HANDLE;
}

bool VkBackend::_recreate_swapchain(int fb_w, int fb_h) {
    vkDeviceWaitIdle(_device);
    _destroy_swapchain();
    return _create_swapchain(fb_w, fb_h) && _create_framebuffers();
}

// ---------------------------------------------------------------------------
// Pipeline creation (lazy, per shader + vertex layout)
// ---------------------------------------------------------------------------

VkPipeline VkBackend::_get_or_create_pipeline(ShaderHandle shader,
                                                const VertexLayout& layout) {
    PipelineKey key{ shader, _layout_hash(layout) };
    auto it = _pipelines.find(key);
    if (it != _pipelines.end()) return it->second;

    auto sit = _shaders.find(shader);
    if (sit == _shaders.end()) return VK_NULL_HANDLE;

    // Shader stages.
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = sit->second.vert_module;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = sit->second.frag_module;
    stages[1].pName  = "main";

    // Vertex input.
    std::vector<VkVertexInputAttributeDescription> attrs;
    for (int i = 0; i < layout.attr_count; ++i) {
        VkVertexInputAttributeDescription d{};
        d.location = (uint32_t)layout.attrs[i].location;
        d.binding  = 0;
        d.format   = (layout.attrs[i].components == 4) ? VK_FORMAT_R32G32B32A32_SFLOAT
                   : (layout.attrs[i].components == 3) ? VK_FORMAT_R32G32B32_SFLOAT
                   : (layout.attrs[i].components == 2) ? VK_FORMAT_R32G32_SFLOAT
                                                       : VK_FORMAT_R32_SFLOAT;
        d.offset   = (uint32_t)layout.attrs[i].offset;
        attrs.push_back(d);
    }
    VkVertexInputBindingDescription binding{ 0, (uint32_t)layout.stride,
                                             VK_VERTEX_INPUT_RATE_VERTEX };

    VkPipelineVertexInputStateCreateInfo vert_in{};
    vert_in.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vert_in.vertexBindingDescriptionCount   = 1;
    vert_in.pVertexBindingDescriptions      = &binding;
    vert_in.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vert_in.pVertexAttributeDescriptions    = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp_state{};
    vp_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.viewportCount = 1;
    vp_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType     = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Blend state: standard alpha blending or opaque.
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable         = _blend_alpha ? VK_TRUE : VK_FALSE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp        = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.alphaBlendOp        = VK_BLEND_OP_ADD;
    blend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend_state{};
    blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend_state.attachmentCount = 1;
    blend_state.pAttachments    = &blend;

    // Dynamic viewport and scissor.
    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dyn_states;

    VkGraphicsPipelineCreateInfo gfx_ci{};
    gfx_ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gfx_ci.stageCount          = 2;
    gfx_ci.pStages             = stages;
    gfx_ci.pVertexInputState   = &vert_in;
    gfx_ci.pInputAssemblyState = &ia;
    gfx_ci.pViewportState      = &vp_state;
    gfx_ci.pRasterizationState = &raster;
    gfx_ci.pMultisampleState   = &ms;
    gfx_ci.pColorBlendState    = &blend_state;
    gfx_ci.pDynamicState       = &dyn;
    gfx_ci.layout              = _pipeline_layout;
    gfx_ci.renderPass          = _render_pass;

    VkPipeline pipeline = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(_device, _pipeline_cache, 1,
                              &gfx_ci, nullptr, &pipeline);
    _pipelines[key] = pipeline;
    return pipeline;
}

// ---------------------------------------------------------------------------
// GLSL → SPIR-V compilation via glslang (Vulkan SDK)
// ---------------------------------------------------------------------------

std::vector<uint32_t> VkBackend::_compile_glsl(const char* src,
                                                 bool is_fragment) const {
    glslang_stage_t stage = is_fragment ? GLSLANG_STAGE_FRAGMENT
                                        : GLSLANG_STAGE_VERTEX;

    glslang_input_t input{};
    input.language                          = GLSLANG_SOURCE_GLSL;
    input.stage                             = stage;
    input.client                            = GLSLANG_CLIENT_VULKAN;
    input.client_version                    = GLSLANG_TARGET_VULKAN_1_2;
    input.target_language                   = GLSLANG_TARGET_SPV;
    input.target_language_version           = GLSLANG_TARGET_SPV_1_5;
    input.code                              = src;
    input.default_version                   = 450;
    input.default_profile                   = GLSLANG_CORE_PROFILE;
    input.force_default_version_and_profile = 0;
    input.forward_compatible                = 0;
    input.messages                          = GLSLANG_MSG_DEFAULT_BIT;
    input.resource                          = glslang_default_resource();

    glslang_shader_t* shader = glslang_shader_create(&input);

    auto fail_shader = [&](const char* step) -> std::vector<uint32_t> {
        std::cerr << "[VkBackend] glslang " << step << ": "
                  << glslang_shader_get_info_log(shader) << "\n";
        // Print source with line numbers to identify the offending line.
        std::istringstream dbg(src);
        std::string dbg_line;
        int ln = 1;
        while (std::getline(dbg, dbg_line))
            std::cerr << "  " << ln++ << ": " << dbg_line << "\n";
        glslang_shader_delete(shader);
        return {};
    };

    if (!glslang_shader_preprocess(shader, &input)) return fail_shader("preprocess");
    if (!glslang_shader_parse(shader, &input))      return fail_shader("parse");

    glslang_program_t* program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT |
                                       GLSLANG_MSG_VULKAN_RULES_BIT)) {
        std::cerr << "[VkBackend] glslang link: "
                  << glslang_program_get_info_log(program) << "\n";
        std::istringstream dbg(src);
        std::string dbg_line;
        int ln = 1;
        while (std::getline(dbg, dbg_line))
            std::cerr << "  " << ln++ << ": " << dbg_line << "\n";
        glslang_program_delete(program);
        glslang_shader_delete(shader);
        return {};
    }

    glslang_program_SPIRV_generate(program, stage);

    const char* spirv_msg = glslang_program_SPIRV_get_messages(program);
    if (spirv_msg && spirv_msg[0])
        std::cerr << "[VkBackend] glslang SPIRV: " << spirv_msg << "\n";

    size_t spirv_size = glslang_program_SPIRV_get_size(program);
    std::vector<uint32_t> out(spirv_size);
    glslang_program_SPIRV_get(program, out.data());

    glslang_program_delete(program);
    glslang_shader_delete(shader);
    return out;
}

// ---------------------------------------------------------------------------
// Memory helpers
// ---------------------------------------------------------------------------

uint32_t VkBackend::_find_memory_type(uint32_t filter,
                                       VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(_phys_dev, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
        if ((filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return UINT32_MAX;
}

bool VkBackend::_create_buffer_vk(size_t size, VkBufferUsageFlags usage,
                                   VkMemoryPropertyFlags props,
                                   VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size  = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(_device, &ci, nullptr, &buf) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(_device, buf, &req);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = req.size;
    alloc_info.memoryTypeIndex = _find_memory_type(req.memoryTypeBits, props);

    if (vkAllocateMemory(_device, &alloc_info, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(_device, buf, mem, 0);
    return true;
}

bool VkBackend::_create_image_vk(int w, int h, VkFormat fmt,
                                   VkImageUsageFlags usage,
                                   VkImage& img, VkDeviceMemory& mem) {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = fmt;
    ci.extent        = { (uint32_t)w, (uint32_t)h, 1 };
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = usage;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(_device, &ci, nullptr, &img) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(_device, img, &req);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = req.size;
    alloc_info.memoryTypeIndex = _find_memory_type(req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(_device, &alloc_info, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindImageMemory(_device, img, mem, 0);
    return true;
}

VkCommandBuffer VkBackend::_begin_transfer_cmd() {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = _cmd_pool;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(_device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    return cmd;
}

void VkBackend::_end_transfer_cmd(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(_gfx_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(_gfx_queue);
    vkFreeCommandBuffers(_device, _cmd_pool, 1, &cmd);
}

void VkBackend::_transition_image_layout(VkCommandBuffer cmd, VkImage img,
                                          VkImageLayout from,
                                          VkImageLayout to) {
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = from;
    barrier.newLayout           = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = img;
    barrier.subresourceRange.aspectMask  = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount  = 1;
    barrier.subresourceRange.layerCount  = 1;

    VkPipelineStageFlags src_stage, dst_stage;
    if (from == VK_IMAGE_LAYOUT_UNDEFINED &&
        to   == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {  // TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL (and updates)
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

uint32_t VkBackend::_layout_hash(const VertexLayout& l) noexcept {
    uint32_t h = (uint32_t)l.stride * 2654435761u ^ (uint32_t)l.attr_count;
    for (int i = 0; i < l.attr_count; ++i) {
        h ^= (uint32_t)l.attrs[i].location   * 2246822519u;
        h ^= (uint32_t)l.attrs[i].components * 3266489917u;
        h ^= (uint32_t)l.attrs[i].offset     * 374761393u;
    }
    return h;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VkBackend::_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[Vulkan] " << data->pMessage << "\n";
    return VK_FALSE;
}

} // namespace bl_ui::gfx

#endif  // BLUI_VULKAN_BACKEND
