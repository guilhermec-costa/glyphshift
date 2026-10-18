#pragma once
#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

// Device functions and resource helpers shared by the Vulkan glyph pipeline
// and its hosts (the loader layer today). No window system and no layer
// dispatch: the device comes with its own procedure-address function.
namespace glyphshift::vulkan {
#define GLYPHSHIFT_VK_RENDER_FUNCTIONS(X) \
    X(CreateImage)                        \
    X(DestroyImage)                       \
    X(GetImageMemoryRequirements)         \
    X(AllocateMemory)                     \
    X(FreeMemory)                         \
    X(BindImageMemory)                    \
    X(CreateImageView)                    \
    X(DestroyImageView)                   \
    X(CreateBuffer)                       \
    X(DestroyBuffer)                      \
    X(GetBufferMemoryRequirements)        \
    X(BindBufferMemory)                   \
    X(MapMemory)                          \
    X(UnmapMemory)                        \
    X(CreateSampler)                      \
    X(DestroySampler)                     \
    X(CreateDescriptorSetLayout)          \
    X(DestroyDescriptorSetLayout)         \
    X(CreateDescriptorPool)               \
    X(DestroyDescriptorPool)              \
    X(AllocateDescriptorSets)             \
    X(UpdateDescriptorSets)               \
    X(CreatePipelineLayout)               \
    X(DestroyPipelineLayout)              \
    X(CreateShaderModule)                 \
    X(DestroyShaderModule)                \
    X(CreateRenderPass)                   \
    X(DestroyRenderPass)                  \
    X(CreateGraphicsPipelines)            \
    X(DestroyPipeline)                    \
    X(CreateFramebuffer)                  \
    X(DestroyFramebuffer)                 \
    X(CreateCommandPool)                  \
    X(DestroyCommandPool)                 \
    X(AllocateCommandBuffers)             \
    X(ResetCommandBuffer)                 \
    X(BeginCommandBuffer)                 \
    X(EndCommandBuffer)                   \
    X(CmdPipelineBarrier)                 \
    X(CmdCopyImage)                       \
    X(CmdCopyBufferToImage)               \
    X(CmdBeginRenderPass)                 \
    X(CmdEndRenderPass)                   \
    X(CmdBindPipeline)                    \
    X(CmdSetViewport)                     \
    X(CmdSetScissor)                      \
    X(CmdBindDescriptorSets)              \
    X(CmdPushConstants)                   \
    X(CmdBindVertexBuffers)               \
    X(CmdBindIndexBuffer)                 \
    X(CmdDrawIndexed)                     \
    X(CmdDraw)                            \
    X(CreateSemaphore)                    \
    X(DestroySemaphore)                   \
    X(CreateFence)                        \
    X(DestroyFence)                       \
    X(WaitForFences)                      \
    X(ResetFences)                        \
    X(QueueSubmit)                        \
    X(QueueWaitIdle)

struct RenderApi {
#define MEMBER(name) PFN_vk##name name = nullptr;
    GLYPHSHIFT_VK_RENDER_FUNCTIONS(MEMBER)
#undef MEMBER
    bool available = false;
};

// Resolves the table through the device's procedure-address function.
RenderApi load_render_api(PFN_vkGetDeviceProcAddr get_proc, VkDevice device);

// A logical device as the pipeline uses it.
struct Device {
    VkDevice handle = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    const RenderApi* api = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties memory_properties = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties format_properties = nullptr;
};

template <class T>
T info(VkStructureType type) {
    T result{};
    result.sType = type;
    return result;
}

inline void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": VkResult " + std::to_string(result));
}

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

// Creates and destroys device objects of one device.
class Resources {
public:
    explicit Resources(const Device& device);

    const Device& device() const noexcept {
        return device_;
    }

    const RenderApi& api() const noexcept {
        return *device_.api;
    }

    // Optimal-tiling features of `format` on this device.
    VkFormatFeatureFlags features(VkFormat format) const;

    void allocate(VkMemoryRequirements requirements,
        VkMemoryPropertyFlags flags,
        VkDeviceMemory& output) const;
    VkImageView image_view(VkImage image, VkFormat format) const;
    void texture(Texture& target,
        VkFormat format,
        VkExtent2D extent,
        VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT) const;
    void destroy(Texture& texture) const;
    void barrier(VkCommandBuffer command,
        VkImage image,
        VkImageLayout old_layout,
        VkImageLayout new_layout,
        VkAccessFlags source,
        VkAccessFlags destination,
        VkPipelineStageFlags source_stage,
        VkPipelineStageFlags destination_stage) const;

private:
    Device device_;
    VkPhysicalDeviceMemoryProperties memory_{};
};
}  // namespace glyphshift::vulkan
