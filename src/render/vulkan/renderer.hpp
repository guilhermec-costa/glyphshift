#pragma once
#include <vulkan/vk_layer.h>

#include <glyphshift/config/settings.hpp>
#include <memory>

#include "resources.hpp"

namespace glyphshift::vulkan {
struct DeviceDispatch;
struct SwapchainState;
struct QueueState;
class GlyphRenderer;
void initialize_renderer_api(DeviceDispatch& state);
bool negotiate_capture(DeviceDispatch& state,
    const VkSwapchainCreateInfoKHR& input,
    VkSwapchainCreateInfoKHR& output) noexcept;
VkResult render_present(DeviceDispatch& state, VkQueue queue, const VkPresentInfoKHR* info);
void release_renderers(DeviceDispatch& state);
}  // namespace glyphshift::vulkan
