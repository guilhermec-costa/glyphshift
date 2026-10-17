#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <glyphshift/config/settings.hpp>
#include <glyphshift/editor/editor.hpp>
#include <new>

#include "dispatch.hpp"
#include "present_chain.hpp"

namespace glyphshift::vulkan {
namespace {
template <class Chain>
Chain* chain_info(const void* next, VkStructureType type) {
  while (next) {
    const auto* base = static_cast<const VkBaseInStructure*>(next);
    if (base->sType == type) {
      auto* chain = const_cast<Chain*>(static_cast<const Chain*>(next));
      if (chain->function == VK_LAYER_LINK_INFO) return chain;
    }
    next = base->pNext;
  }
  return nullptr;
}

VKAPI_ATTR VkResult VKAPI_CALL
CreateInstance(const VkInstanceCreateInfo* info,
               const VkAllocationCallbacks* allocator, VkInstance* instance) {
  auto* chain = chain_info<VkLayerInstanceCreateInfo>(
      info->pNext, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO);
  if (!chain || !chain->u.pLayerInfo ||
      !chain->u.pLayerInfo->pfnNextGetInstanceProcAddr)
    return VK_ERROR_INITIALIZATION_FAILED;
  std::shared_ptr<InstanceDispatch> state;
  try {
    state = std::make_shared<InstanceDispatch>();
  } catch (const std::bad_alloc&) {
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  state->get_proc = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  state->get_physical_proc =
      chain->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
  const auto create = reinterpret_cast<PFN_vkCreateInstance>(
      state->get_proc(VK_NULL_HANDLE, "vkCreateInstance"));
  if (!create) return VK_ERROR_INITIALIZATION_FAILED;
  chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
  const auto result = create(info, allocator, instance);
  if (result != VK_SUCCESS) return result;
  state->handle = *instance;
  initialize(*state);
  try {
    register_instance(state);
  } catch (const std::bad_alloc&) {
    state->destroy(*instance, allocator);
    *instance = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  diagnostic("glyphshift: Vulkan pass-through instance created\n");
  return result;
}
VKAPI_ATTR void VKAPI_CALL
DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
  const auto state = find_instance(dispatch_key(instance));
  if (!state) return;
  remove_instance(instance);
  state->destroy(instance, allocator);
  diagnostic("glyphshift: Vulkan instance destroyed\n");
}
void record_physical(const std::shared_ptr<InstanceDispatch>& state,
                     uint32_t count, const VkPhysicalDevice* devices) {
  try {
    const std::lock_guard lock(state->mutex);
    state->physical_devices.insert(devices, devices + count);
  } catch (const std::bad_alloc&) {
    metadata_failure(state->metadata_warning);
  }
}
VKAPI_ATTR VkResult VKAPI_CALL EnumeratePhysicalDevices(
    VkInstance instance, uint32_t* count, VkPhysicalDevice* devices) {
  const auto state = find_instance(dispatch_key(instance));
  if (!state) return VK_ERROR_INITIALIZATION_FAILED;
  const auto result = state->enumerate_physical(instance, count, devices);
  if (devices && (result == VK_SUCCESS || result == VK_INCOMPLETE))
    record_physical(state, *count, devices);
  return result;
}
template <class Enumerate>
VkResult enumerate_groups(const std::shared_ptr<InstanceDispatch>& state,
                          Enumerate next, VkInstance instance, uint32_t* count,
                          VkPhysicalDeviceGroupProperties* groups) {
  const auto result = next(instance, count, groups);
  if (groups && (result == VK_SUCCESS || result == VK_INCOMPLETE)) {
    for (uint32_t i = 0; i < *count; ++i)
      record_physical(state, groups[i].physicalDeviceCount,
                      groups[i].physicalDevices);
  }
  return result;
}
VKAPI_ATTR VkResult VKAPI_CALL
EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* count,
                              VkPhysicalDeviceGroupProperties* groups) {
  const auto state = find_instance(dispatch_key(instance));
  if (!state || !state->enumerate_groups) return VK_ERROR_INITIALIZATION_FAILED;
  return enumerate_groups(state, state->enumerate_groups, instance, count,
                          groups);
}
VKAPI_ATTR VkResult VKAPI_CALL
EnumeratePhysicalDeviceGroupsKHR(VkInstance instance, uint32_t* count,
                                 VkPhysicalDeviceGroupProperties* groups) {
  const auto state = find_instance(dispatch_key(instance));
  if (!state || !state->enumerate_groups_khr)
    return VK_ERROR_INITIALIZATION_FAILED;
  return enumerate_groups(state, state->enumerate_groups_khr, instance, count,
                          groups);
}
VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(
    VkPhysicalDevice physical, const char* name, uint32_t* count,
    VkExtensionProperties* properties) {
  if (name && std::strcmp(name, layer_name) == 0) {
    *count = 0;  // M2 provides no new extensions.
    return VK_SUCCESS;
  }
  const auto state = find_instance(dispatch_key(physical));
  if (!state) return VK_ERROR_INITIALIZATION_FAILED;
  return state->enumerate_extensions(physical, name, count, properties);
}
VKAPI_ATTR VkResult VKAPI_CALL
CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
             const VkAllocationCallbacks* allocator, VkDevice* device) {
  const auto parent = find_instance(dispatch_key(physical));
  if (!parent) return VK_ERROR_INITIALIZATION_FAILED;
  auto* chain = chain_info<VkLayerDeviceCreateInfo>(
      info->pNext, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO);
  if (!chain || !chain->u.pLayerInfo ||
      !chain->u.pLayerInfo->pfnNextGetInstanceProcAddr ||
      !chain->u.pLayerInfo->pfnNextGetDeviceProcAddr)
    return VK_ERROR_INITIALIZATION_FAILED;
  const auto create = reinterpret_cast<PFN_vkCreateDevice>(
      chain->u.pLayerInfo->pfnNextGetInstanceProcAddr(parent->handle,
                                                      "vkCreateDevice"));
  if (!create) return VK_ERROR_INITIALIZATION_FAILED;
  std::shared_ptr<DeviceDispatch> state;
  try {
    state = std::make_shared<DeviceDispatch>();
  } catch (const std::bad_alloc&) {
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  state->physical = physical;
  if (parent->properties) {
    VkPhysicalDeviceProperties properties{};
    parent->properties(physical, &properties);
    std::snprintf(state->version.data(), state->version.size(), "%u.%u.%u",
                  VK_API_VERSION_MAJOR(properties.apiVersion),
                  VK_API_VERSION_MINOR(properties.apiVersion),
                  VK_API_VERSION_PATCH(properties.apiVersion));
  }
  for (auto* next = static_cast<const VkBaseInStructure*>(info->pNext); next;
       next = next->pNext) {
    if (next->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
      const auto* loader =
          reinterpret_cast<const VkLayerDeviceCreateInfo*>(next);
      if (loader->function == VK_LOADER_DATA_CALLBACK)
        state->set_loader_data = loader->u.pfnSetDeviceLoaderData;
    }
  }
  state->instance = parent;
  state->get_proc = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
  const auto result = create(physical, info, allocator, device);
  if (result != VK_SUCCESS) return result;
  state->handle = *device;
  initialize(*state);
  try {
    register_device(state);
  } catch (const std::bad_alloc&) {
    state->destroy(*device, allocator);
    *device = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  diagnostic("glyphshift: Vulkan device created\n");
  return result;
}
VKAPI_ATTR void VKAPI_CALL
DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
  const auto state = find_device(dispatch_key(device));
  if (!state) return;
  remove_device(device);
  release_renderers(*state);
  state->destroy(device, allocator);
  diagnostic("glyphshift: Vulkan device destroyed\n");
}
void record_queue(const std::shared_ptr<DeviceDispatch>& state, VkQueue queue,
                  QueueState metadata) {
  if (!queue) return;
  try {
    const std::lock_guard lock(state->mutex);
    state->queues.insert_or_assign(queue, metadata);
  } catch (const std::bad_alloc&) {
    metadata_failure(state->metadata_warning);
  }
}
VKAPI_ATTR void VKAPI_CALL GetDeviceQueue(VkDevice device, uint32_t family,
                                          uint32_t index, VkQueue* queue) {
  const auto state = find_device(dispatch_key(device));
  if (!state) return;
  state->get_queue(device, family, index, queue);
  record_queue(state, *queue, {family, index, 0});
}
VKAPI_ATTR void VKAPI_CALL GetDeviceQueue2(VkDevice device,
                                           const VkDeviceQueueInfo2* info,
                                           VkQueue* queue) {
  const auto state = find_device(dispatch_key(device));
  if (!state || !state->get_queue2) return;
  state->get_queue2(device, info, queue);
  record_queue(state, *queue,
               {info->queueFamilyIndex, info->queueIndex, info->flags});
}
VKAPI_ATTR VkResult VKAPI_CALL CreateXlibSurfaceKHR(
    VkInstance instance, const VkXlibSurfaceCreateInfoKHR* info,
    const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
  const auto state = find_instance(dispatch_key(instance));
  if (!state) return VK_ERROR_INITIALIZATION_FAILED;
  const auto next = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
      state->get_proc(instance, "vkCreateXlibSurfaceKHR"));
  if (!next) return VK_ERROR_EXTENSION_NOT_PRESENT;
  const auto result = next(instance, info, allocator, surface);
  if (result == VK_SUCCESS && diagnostics_enabled()) {
    char message[96];
    std::snprintf(message, sizeof(message),
                  "glyphshift: Vulkan Xlib window 0x%lx\n", info->window);
    diagnostic(message);
  }
  if (result == VK_SUCCESS) {
    try {
      const std::lock_guard lock(state->mutex);
      state->surfaces.emplace(
          *surface, std::pair<void*, std::uint64_t>{info->dpy, info->window});
    } catch (...) {
      metadata_failure(state->metadata_warning);
    }
  }
  return result;
}
VKAPI_ATTR void VKAPI_CALL
DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                  const VkAllocationCallbacks* allocator) {
  const auto state = find_instance(dispatch_key(instance));
  if (!state) return;
  std::pair<void*, std::uint64_t> window{};
  {
    const std::lock_guard lock(state->mutex);
    const auto found = state->surfaces.find(surface);
    if (found != state->surfaces.end()) {
      window = found->second;
      state->surfaces.erase(found);
    }
  }
  // Retire editor input outside the registry lock, including when the host
  // resolves Xlib privately and bypasses our window-destruction hooks.
  if (window.first) editor::forget(window.first, window.second);
  const auto next = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
      state->get_proc(instance, "vkDestroySurfaceKHR"));
  if (next) next(instance, surface, allocator);
}
void record_swapchain(const std::shared_ptr<DeviceDispatch>& state,
                      const VkSwapchainCreateInfoKHR& info,
                      VkSwapchainKHR swapchain, bool allow_capture = true) {
  try {
    auto metadata = std::make_shared<SwapchainState>();
    {
      const std::lock_guard lock(state->instance->mutex);
      const auto surface = state->instance->surfaces.find(info.surface);
      if (surface != state->instance->surfaces.end()) {
        metadata->display = surface->second.first;
        metadata->window = surface->second.second;
      }
    }
    metadata->format = info.imageFormat;
    metadata->extent = info.imageExtent;
    metadata->capture =
        allow_capture && (info.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
        (info.imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
        info.imageArrayLayers == 1 && supported_swapchain_flags(info.flags) &&
        info.imageColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    metadata->sharing = info.imageSharingMode;
    if (info.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
      if (info.queueFamilyIndexCount > metadata->families.size())
        metadata->capture = false;
      else {
        metadata->family_count = info.queueFamilyIndexCount;
        for (uint32_t i = 0; i < metadata->family_count; ++i)
          metadata->families[i] = info.pQueueFamilyIndices[i];
      }
    }
    const std::lock_guard lock(state->mutex);
    state->swapchains.emplace(swapchain, std::move(metadata));
  } catch (const std::bad_alloc&) {
    metadata_failure(state->metadata_warning);
  }
  diagnostic("glyphshift: Vulkan swapchain created\n");
  char details[160];
  std::snprintf(details, sizeof(details),
                "glyphshift: Vulkan swapchain flags=0x%x format=%d usage=0x%x "
                "layers=%u colorspace=%d\n",
                info.flags, static_cast<int>(info.imageFormat), info.imageUsage,
                info.imageArrayLayers, static_cast<int>(info.imageColorSpace));
  diagnostic(details);
}
VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR* info,
    const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain) {
  const auto state = find_device(dispatch_key(device));
  if (!state || !state->create_swapchain) return VK_ERROR_INITIALIZATION_FAILED;
  VkSwapchainCreateInfoKHR capture_info{};
  const bool capture = negotiate_capture(*state, *info, capture_info);
  const auto* forwarded_info = capture ? &capture_info : info;
  auto result =
      state->create_swapchain(device, forwarded_info, allocator, swapchain);
  bool allow_capture = true;
  if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST && capture &&
      forwarded_info->imageUsage != info->imageUsage) {
    diagnostic(
        "glyphshift: Vulkan capture usage rejected; retrying original "
        "swapchain\n");
    result = state->create_swapchain(device, info, allocator, swapchain);
    forwarded_info = info;
    allow_capture = false;
  }
  if (result == VK_SUCCESS)
    record_swapchain(state, *forwarded_info, *swapchain, allow_capture);
  return result;
}
VKAPI_ATTR VkResult VKAPI_CALL CreateSharedSwapchainsKHR(
    VkDevice device, uint32_t count, const VkSwapchainCreateInfoKHR* infos,
    const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchains) {
  const auto state = find_device(dispatch_key(device));
  if (!state || !state->create_shared_swapchains)
    return VK_ERROR_INITIALIZATION_FAILED;
  const auto result = state->create_shared_swapchains(device, count, infos,
                                                      allocator, swapchains);
  if (result == VK_SUCCESS) {
    for (uint32_t i = 0; i < count; ++i)
      record_swapchain(state, infos[i], swapchains[i], false);
  }
  return result;
}
VKAPI_ATTR void VKAPI_CALL
DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                    const VkAllocationCallbacks* allocator) {
  const auto state = find_device(dispatch_key(device));
  if (!state || !state->destroy_swapchain) return;
  std::shared_ptr<SwapchainState> retired;
  {
    const std::lock_guard lock(state->mutex);
    const auto found = state->swapchains.find(swapchain);
    if (found != state->swapchains.end()) {
      retired = std::move(found->second);
      state->swapchains.erase(found);
    }
  }
  // Free layer-owned views/framebuffers while the app's swapchain still lives.
  retired.reset();
  state->destroy_swapchain(device, swapchain, allocator);
  if (swapchain) diagnostic("glyphshift: Vulkan swapchain destroyed\n");
}
VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainImagesKHR(VkDevice device,
                                                     VkSwapchainKHR swapchain,
                                                     uint32_t* count,
                                                     VkImage* images) {
  const auto state = find_device(dispatch_key(device));
  if (!state || !state->get_images) return VK_ERROR_INITIALIZATION_FAILED;
  const auto result = state->get_images(device, swapchain, count, images);
  if (images && (result == VK_SUCCESS || result == VK_INCOMPLETE)) {
    try {
      const std::lock_guard lock(state->mutex);
      const auto found = state->swapchains.find(swapchain);
      if (found != state->swapchains.end()) {
        found->second->images.assign(images, images + *count);
        found->second->images_complete = result == VK_SUCCESS;
      }
    } catch (const std::bad_alloc&) {
      metadata_failure(state->metadata_warning);
    }
  }
  return result;
}
VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue,
                                               const VkPresentInfoKHR* info) {
  const auto state = find_device(dispatch_key(queue));
  if (!state || !state->present) return VK_ERROR_INITIALIZATION_FAILED;
  if (diagnostics_enabled() &&
      !state->presented.test_and_set(std::memory_order_relaxed))
    diagnostic("glyphshift: Vulkan present forwarded\n");
  try {
    return render_present(*state, queue, info);
  } catch (const std::exception& error) {
    editor::report(error.what());
    if (!state->render_warning.test_and_set())
      diagnostic(
          "glyphshift: editor unavailable; forwarding presentation "
          "unchanged\n");
    return state->present(queue, info);
  }
}
VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(
    const char* name, uint32_t* count, VkExtensionProperties*) {
  if (!name || std::strcmp(name, layer_name) != 0)
    return VK_ERROR_LAYER_NOT_PRESENT;
  *count = 0;
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceLayerProperties(
    uint32_t* count, VkLayerProperties* properties) {
  if (!properties) {
    *count = 1;
    return VK_SUCCESS;
  }
  if (!*count) return VK_INCOMPLETE;
  VkLayerProperties value{};
  std::strcpy(value.layerName, layer_name);
  value.specVersion = VK_HEADER_VERSION_COMPLETE;
  value.implementationVersion = 1;
  std::strcpy(value.description, "GlyphShift Vulkan pass-through");
  *properties = value;
  *count = 1;
  return VK_SUCCESS;
}
}  // namespace

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
GetInstanceProcAddr(VkInstance instance, const char* name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device,
                                                           const char* name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
GetPhysicalDeviceProcAddr(VkInstance instance, const char* name);

namespace {
PFN_vkVoidFunction device_intercept(const char* name) {
#define INTERCEPT(function)                   \
  if (std::strcmp(name, "vk" #function) == 0) \
  return reinterpret_cast<PFN_vkVoidFunction>(function)
  INTERCEPT(DestroyDevice);
  INTERCEPT(GetDeviceQueue);
  INTERCEPT(GetDeviceQueue2);
  INTERCEPT(CreateSwapchainKHR);
  INTERCEPT(CreateSharedSwapchainsKHR);
  INTERCEPT(DestroySwapchainKHR);
  INTERCEPT(GetSwapchainImagesKHR);
  INTERCEPT(QueuePresentKHR);
  INTERCEPT(GetDeviceProcAddr);
#undef INTERCEPT
  return nullptr;
}
PFN_vkVoidFunction instance_intercept(const char* name) {
#define INTERCEPT(function)                   \
  if (std::strcmp(name, "vk" #function) == 0) \
  return reinterpret_cast<PFN_vkVoidFunction>(function)
  INTERCEPT(CreateXlibSurfaceKHR);
  INTERCEPT(DestroySurfaceKHR);
  INTERCEPT(DestroyInstance);
  INTERCEPT(EnumeratePhysicalDevices);
  INTERCEPT(EnumeratePhysicalDeviceGroups);
  INTERCEPT(EnumeratePhysicalDeviceGroupsKHR);
  INTERCEPT(EnumerateDeviceExtensionProperties);
  INTERCEPT(CreateDevice);
#undef INTERCEPT
  return nullptr;
}
}  // namespace
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
GetInstanceProcAddr(VkInstance instance, const char* name) {
  if (!name) return nullptr;
  if (std::strcmp(name, "vkGetInstanceProcAddr") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(GetInstanceProcAddr);
  if (std::strcmp(name, "vkCreateInstance") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(CreateInstance);
  if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(
        EnumerateInstanceLayerProperties);
  if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(
        EnumerateInstanceExtensionProperties);
  if (!instance) return nullptr;
  const auto state = find_instance(dispatch_key(instance));
  if (!state) return nullptr;
  if (std::strcmp(name, "vk_layerGetPhysicalDeviceProcAddr") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceProcAddr);
  const auto next = state->get_proc(instance, name);
  if (!next) return nullptr;
  if (const auto own = instance_intercept(name)) return own;
  if (const auto own = device_intercept(name)) return own;
  return next;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device,
                                                           const char* name) {
  if (!device || !name) return nullptr;
  const auto state = find_device(dispatch_key(device));
  if (!state) return nullptr;
  const auto next = state->get_proc(device, name);
  if (!next) return nullptr;
  if (const auto own = device_intercept(name)) return own;
  return next;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
GetPhysicalDeviceProcAddr(VkInstance instance, const char* name) {
  if (!instance || !name) return nullptr;
  const auto state = find_instance(dispatch_key(instance));
  if (!state) return nullptr;
  // Known physical-device commands are intercepted independently of whether
  // an older downstream layer supplies the physical-extension query hook.
  if (std::strcmp(name, "vkCreateDevice") == 0 ||
      std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0) {
    return state->get_proc(instance, name) ? instance_intercept(name) : nullptr;
  }
  // Never use GIPA as a fallback for an unknown physical command: it could
  // return a device command, misclassifying it in the loader's dispatch chain.
  return state->get_physical_proc ? state->get_physical_proc(instance, name)
                                  : nullptr;
}
}  // namespace glyphshift::vulkan

extern "C" __attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* info) {
  if (!info || info->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT ||
      info->loaderLayerInterfaceVersion < 2)
    return VK_ERROR_INITIALIZATION_FAILED;
  info->loaderLayerInterfaceVersion = 2;
  info->pfnGetInstanceProcAddr = glyphshift::vulkan::GetInstanceProcAddr;
  info->pfnGetDeviceProcAddr = glyphshift::vulkan::GetDeviceProcAddr;
  info->pfnGetPhysicalDeviceProcAddr =
      glyphshift::vulkan::GetPhysicalDeviceProcAddr;
  return VK_SUCCESS;
}
