#include "dispatch.hpp"

#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace glyphshift::vulkan {
namespace {
struct Registry {
    std::mutex mutex;
    std::unordered_map<void*, std::shared_ptr<InstanceDispatch>> instances;
    std::unordered_map<void*, std::shared_ptr<DeviceDispatch>> devices;
};

Registry& registry() {
    static Registry value;
    return value;
}
}  // namespace

std::shared_ptr<InstanceDispatch> find_instance(void* key) {
    auto& data = registry();
    const std::lock_guard lock(data.mutex);
    const auto found = data.instances.find(key);
    return found == data.instances.end() ? nullptr : found->second;
}

std::shared_ptr<DeviceDispatch> find_device(void* key) {
    auto& data = registry();
    const std::lock_guard lock(data.mutex);
    const auto found = data.devices.find(key);
    return found == data.devices.end() ? nullptr : found->second;
}

void register_instance(const std::shared_ptr<InstanceDispatch>& state) {
    auto& data = registry();
    const std::lock_guard lock(data.mutex);
    data.instances.emplace(dispatch_key(state->handle), state);
}

void register_device(const std::shared_ptr<DeviceDispatch>& state) {
    auto& data = registry();
    const std::lock_guard lock(data.mutex);
    data.devices.emplace(dispatch_key(state->handle), state);
}

void remove_instance(VkInstance instance) {
    auto& data = registry();
    const std::lock_guard lock(data.mutex);
    data.instances.erase(dispatch_key(instance));
}

void remove_device(VkDevice device) {
    auto& data = registry();
    const std::lock_guard lock(data.mutex);
    data.devices.erase(dispatch_key(device));
}

void initialize(InstanceDispatch& state) {
    if (!state.get_physical_proc)
        state.get_physical_proc = reinterpret_cast<PFN_GetPhysicalDeviceProcAddr>(
            state.get_proc(state.handle, "vk_layerGetPhysicalDeviceProcAddr"));
#define LOAD(member, name) \
    state.member = reinterpret_cast<PFN_##name>(state.get_proc(state.handle, #name))
    LOAD(destroy, vkDestroyInstance);
    LOAD(properties, vkGetPhysicalDeviceProperties);
    LOAD(memory_properties, vkGetPhysicalDeviceMemoryProperties);
    LOAD(queue_properties, vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD(surface_capabilities, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD(format_properties, vkGetPhysicalDeviceFormatProperties);
    LOAD(enumerate_physical, vkEnumeratePhysicalDevices);
    LOAD(enumerate_groups, vkEnumeratePhysicalDeviceGroups);
    LOAD(enumerate_groups_khr, vkEnumeratePhysicalDeviceGroupsKHR);
    LOAD(enumerate_extensions, vkEnumerateDeviceExtensionProperties);
#undef LOAD
}

void initialize(DeviceDispatch& state) {
#define LOAD(member, name) \
    state.member = reinterpret_cast<PFN_##name>(state.get_proc(state.handle, #name))
    LOAD(destroy, vkDestroyDevice);
    LOAD(get_queue, vkGetDeviceQueue);
    LOAD(get_queue2, vkGetDeviceQueue2);
    LOAD(create_swapchain, vkCreateSwapchainKHR);
    LOAD(create_shared_swapchains, vkCreateSharedSwapchainsKHR);
    LOAD(destroy_swapchain, vkDestroySwapchainKHR);
    LOAD(get_images, vkGetSwapchainImagesKHR);
    LOAD(present, vkQueuePresentKHR);
#undef LOAD
    initialize_renderer_api(state);
}

bool diagnostics_enabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("GLYPHSHIFT_LOG");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

void diagnostic(const char* text) noexcept {
    if (!diagnostics_enabled())
        return;
    const auto ignored = write(STDERR_FILENO, text, std::strlen(text));
    (void)ignored;
}

void metadata_failure(std::atomic_flag& warning) noexcept {
    if (!warning.test_and_set(std::memory_order_relaxed))
        diagnostic(
            "glyphshift: Vulkan metadata allocation failed; forwarding "
            "unchanged\n");
}
}  // namespace glyphshift::vulkan
