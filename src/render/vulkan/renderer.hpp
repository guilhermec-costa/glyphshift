#pragma once
#include <vulkan/vk_layer.h>

#include <glyphshift/config/settings.hpp>
#include <memory>

namespace glyphshift::vulkan {
struct DeviceDispatch;
struct SwapchainState;
struct QueueState;
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
class GlyphRenderer;
void initialize_renderer_api(DeviceDispatch& state);
bool negotiate_capture(DeviceDispatch& state,
    const VkSwapchainCreateInfoKHR& input,
    VkSwapchainCreateInfoKHR& output) noexcept;
VkResult render_present(DeviceDispatch& state, VkQueue queue, const VkPresentInfoKHR* info);
void release_renderers(DeviceDispatch& state);
}  // namespace glyphshift::vulkan
