#pragma once

#include <vulkan/vk_layer.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "renderer.hpp"

namespace glyphshift::vulkan {
inline constexpr char layer_name[] = "VK_LAYER_GLYPHSHIFT_overlay";

// The loader gives instance/physical-device handles the same dispatch key,
// and likewise device/queue handles. Never replace or write that key.
template <class Handle>
void* dispatch_key(Handle handle) {
  return handle ? *reinterpret_cast<void* const*>(handle) : nullptr;
}
struct InstanceDispatch {
  VkInstance handle = VK_NULL_HANDLE;
  PFN_vkGetInstanceProcAddr get_proc = nullptr;
  PFN_GetPhysicalDeviceProcAddr get_physical_proc = nullptr;
  PFN_vkDestroyInstance destroy = nullptr;
  std::unordered_map<VkSurfaceKHR, std::pair<void*, std::uint64_t>> surfaces;
  PFN_vkGetPhysicalDeviceProperties properties = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties memory_properties = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties queue_properties = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR surface_capabilities = nullptr;
  PFN_vkGetPhysicalDeviceFormatProperties format_properties = nullptr;
  PFN_vkEnumeratePhysicalDevices enumerate_physical = nullptr;
  PFN_vkEnumeratePhysicalDeviceGroups enumerate_groups = nullptr;
  PFN_vkEnumeratePhysicalDeviceGroupsKHR enumerate_groups_khr = nullptr;
  PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions = nullptr;
  std::mutex mutex;
  std::unordered_set<VkPhysicalDevice> physical_devices;
  std::atomic_flag metadata_warning = ATOMIC_FLAG_INIT;
};
struct QueueState {
  uint32_t family = 0;
  uint32_t index = 0;
  VkDeviceQueueCreateFlags flags = 0;
};
struct SwapchainState {
  std::mutex mutex;
  std::shared_ptr<GlyphRenderer> renderer;
  bool initializing = false, render_failed = false;
  void* display = nullptr;
  std::uint64_t window = 0;
  bool capture = false;
  VkSharingMode sharing = VK_SHARING_MODE_EXCLUSIVE;
  std::array<uint32_t, 32> families{};
  uint32_t family_count = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  std::vector<VkImage> images;
  bool images_complete = false;
};
struct DeviceDispatch {
  VkDevice handle = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  std::array<char, 48> version{};
  PFN_vkSetDeviceLoaderData set_loader_data = nullptr;
  RenderApi render_api;
  bool single_family = false;
  uint32_t only_family = 0;
  std::shared_ptr<InstanceDispatch> instance;
  PFN_vkGetDeviceProcAddr get_proc = nullptr;
  PFN_vkDestroyDevice destroy = nullptr;
  PFN_vkGetDeviceQueue get_queue = nullptr;
  PFN_vkGetDeviceQueue2 get_queue2 = nullptr;
  PFN_vkCreateSwapchainKHR create_swapchain = nullptr;
  PFN_vkCreateSharedSwapchainsKHR create_shared_swapchains = nullptr;
  PFN_vkDestroySwapchainKHR destroy_swapchain = nullptr;
  PFN_vkGetSwapchainImagesKHR get_images = nullptr;
  PFN_vkQueuePresentKHR present = nullptr;
  std::mutex mutex;
  std::unordered_map<VkQueue, QueueState> queues;
  std::unordered_map<VkSwapchainKHR, std::shared_ptr<SwapchainState>>
      swapchains;
  std::atomic_flag metadata_warning = ATOMIC_FLAG_INIT;
  std::atomic_flag presented = ATOMIC_FLAG_INIT;
  std::atomic_flag render_warning = ATOMIC_FLAG_INIT;
};

std::shared_ptr<InstanceDispatch> find_instance(void* key);
std::shared_ptr<DeviceDispatch> find_device(void* key);
void register_instance(const std::shared_ptr<InstanceDispatch>& state);
void register_device(const std::shared_ptr<DeviceDispatch>& state);
void remove_instance(VkInstance instance);
void remove_device(VkDevice device);
void initialize(InstanceDispatch& state);
void initialize(DeviceDispatch& state);
bool diagnostics_enabled() noexcept;
void diagnostic(const char* text) noexcept;
void metadata_failure(std::atomic_flag& warning) noexcept;
}  // namespace glyphshift::vulkan
