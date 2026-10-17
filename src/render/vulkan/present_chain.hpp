#pragma once
#include <vulkan/vulkan.h>

namespace glyphshift::vulkan {
inline bool supported_swapchain_flags(VkSwapchainCreateFlagsKHR flags) noexcept {
    VkSwapchainCreateFlagsKHR metadata = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
#ifdef VK_KHR_present_id2
    metadata |= VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR;
#endif
#ifdef VK_KHR_present_wait2
    metadata |= VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR;
#endif
#ifdef VK_EXT_present_timing
    metadata |= VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
#endif
    return (flags & ~metadata) == 0;
}

// These extensions describe presentation, not the image regions/device masks
// used by our full-image graphics submission. Keep the caller's exact chain
// on the final present, including its fences, IDs and chosen present mode.
// Other extensions still require conservative pass-through.
inline bool supported_present_chain(const void* chain) noexcept {
    auto* node = static_cast<const VkBaseInStructure*>(chain);
    unsigned count = 0;
    for (; node && count < 8; node = node->pNext, ++count) {
        switch (node->sType) {
            case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT:
            case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT:
            case VK_STRUCTURE_TYPE_PRESENT_ID_KHR:
#ifdef VK_KHR_present_id2
            case VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR:
#endif
#ifdef VK_EXT_present_timing
            case VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT:
#endif
                break;
            default:
                return false;
        }
    }
    return node == nullptr;
}
}  // namespace glyphshift::vulkan
