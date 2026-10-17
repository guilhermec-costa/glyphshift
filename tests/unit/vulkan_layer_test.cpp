#include <dlfcn.h>
#include <vulkan/vk_layer.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <thread>

// Fail actual C++ allocations inside the loaded layer, without touching Vulkan
// allocator callbacks. Zero means normal allocation, N fails allocation N.
std::atomic<int> fail_allocation{0};
void* operator new(std::size_t size) {
  int remaining = fail_allocation.load();
  if (remaining > 0 && fail_allocation.fetch_sub(1) == 1)
    throw std::bad_alloc();
  if (void* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

namespace {
int failures = 0;
void check(bool condition, const char* text) {
  if (!condition) {
    std::cerr << "FAIL: " << text << '\n';
    ++failures;
  }
}
struct Object {
  void* dispatch;
};
int instance_tokens[2]{}, device_tokens[2]{};
Object instances[]{{&instance_tokens[0]}, {&instance_tokens[1]}};
Object physicals[]{{&instance_tokens[0]}, {&instance_tokens[1]}};
Object devices[]{{&device_tokens[0]}, {&device_tokens[1]}};
std::array<std::array<Object, 8>, 2> queues{};
VkAllocationCallbacks allocator{};
const void* expected_create = nullptr;
const VkSwapchainCreateInfoKHR* expected_swapchain = nullptr;
VkLayerInstanceCreateInfo* instance_chain = nullptr;
VkLayerDeviceCreateInfo* device_chain = nullptr;
VkResult create_result = VK_SUCCESS;
VkResult swapchain_result = VK_SUCCESS;
VkResult present_result = VK_SUBOPTIMAL_KHR;
std::atomic<unsigned> forwarded{0};
unsigned instance_destroyed[2]{}, device_destroyed[2]{},
    swapchain_destroyed[2]{};
unsigned instance_creates = 0, device_creates = 0;
thread_local const VkPresentInfoKHR* expected_present = nullptr;
thread_local bool bad_present = false;
PFN_vkGetInstanceProcAddr layer_gipa = nullptr;
PFN_vkGetDeviceProcAddr layer_gdpa = nullptr;

template <class Handle>
Handle handle(Object& object) {
  return reinterpret_cast<Handle>(&object);
}
template <class Handle>
Handle opaque(uintptr_t value) {
  return reinterpret_cast<Handle>(value);
}
template <class Function>
PFN_vkVoidFunction pointer(Function function) {
  return reinterpret_cast<PFN_vkVoidFunction>(function);
}
template <class Function>
Function proc(VkInstance instance, const char* name) {
  return reinterpret_cast<Function>(layer_gipa(instance, name));
}

void VKAPI_CALL Unknown() {}
void VKAPI_CALL PhysicalUnknown() {}
VkResult VKAPI_CALL ExtensionProperties(VkPhysicalDevice, const char*,
                                        uint32_t* count,
                                        VkExtensionProperties*) {
  *count = 3;
  return VK_INCOMPLETE;
}
template <int I>
void VKAPI_CALL DestroyInstance(VkInstance instance,
                                const VkAllocationCallbacks* alloc) {
  check(instance == handle<VkInstance>(instances[I]) && alloc == &allocator,
        "destroy instance arguments/routing");
  ++instance_destroyed[I];
}
template <int I>
VkResult VKAPI_CALL PhysicalDevices(VkInstance instance, uint32_t* count,
                                    VkPhysicalDevice* output) {
  check(instance == handle<VkInstance>(instances[I]),
        "physical enumeration instance routing");
  if (output && *count) output[0] = handle<VkPhysicalDevice>(physicals[I]);
  *count = 1;
  return output ? VK_INCOMPLETE : VK_SUCCESS;
}
template <int I>
VkResult VKAPI_CALL Groups(VkInstance, uint32_t* count,
                           VkPhysicalDeviceGroupProperties* groups) {
  if (groups && *count) {
    groups[0].physicalDeviceCount = 1;
    groups[0].physicalDevices[0] = handle<VkPhysicalDevice>(physicals[I]);
  }
  *count = 1;
  return VK_SUCCESS;
}
template <int I>
VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* info,
                                   const VkAllocationCallbacks* alloc,
                                   VkInstance* instance) {
  check(info == expected_create && alloc == &allocator,
        "create instance pointer identity");
  check(instance_chain->u.pLayerInfo == nullptr,
        "instance loader link advanced exactly once");
  ++instance_creates;
  if (create_result == VK_SUCCESS) *instance = handle<VkInstance>(instances[I]);
  return create_result;
}
template <int I>
VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physical,
                                 const VkDeviceCreateInfo* info,
                                 const VkAllocationCallbacks* alloc,
                                 VkDevice* device) {
  check(physical == handle<VkPhysicalDevice>(physicals[I]) &&
            info == expected_create && alloc == &allocator,
        "create device routing and pointer identity");
  check(device_chain->u.pLayerInfo == nullptr,
        "device loader link advanced exactly once");
  ++device_creates;
  if (create_result == VK_SUCCESS) *device = handle<VkDevice>(devices[I]);
  return create_result;
}
template <int I>
void VKAPI_CALL DestroyDevice(VkDevice device,
                              const VkAllocationCallbacks* alloc) {
  check(device == handle<VkDevice>(devices[I]) && alloc == &allocator,
        "destroy device routing and allocator");
  ++device_destroyed[I];
}
template <int I>
void VKAPI_CALL GetQueue(VkDevice device, uint32_t family, uint32_t index,
                         VkQueue* queue) {
  check(device == handle<VkDevice>(devices[I]) && family == 7 && index < 8,
        "queue arguments/routing");
  *queue = handle<VkQueue>(queues[I][index]);
}
template <int I>
void VKAPI_CALL GetQueue2(VkDevice device, const VkDeviceQueueInfo2* info,
                          VkQueue* queue) {
  check(info->flags == VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT,
        "queue2 flags preserved");
  GetQueue<I>(device, info->queueFamilyIndex, info->queueIndex, queue);
}
template <int I>
VkResult VKAPI_CALL CreateSwapchain(VkDevice device,
                                    const VkSwapchainCreateInfoKHR* info,
                                    const VkAllocationCallbacks* alloc,
                                    VkSwapchainKHR* swapchain) {
  check(device == handle<VkDevice>(devices[I]) && info == expected_swapchain &&
            alloc == &allocator,
        "swapchain arguments and pNext forwarded by identity");
  if (swapchain_result == VK_SUCCESS)
    *swapchain = opaque<VkSwapchainKHR>(100 + I);
  return swapchain_result;
}
template <int I>
VkResult VKAPI_CALL CreateSharedSwapchains(
    VkDevice device, uint32_t count, const VkSwapchainCreateInfoKHR* infos,
    const VkAllocationCallbacks* alloc, VkSwapchainKHR* swapchains) {
  check(device == handle<VkDevice>(devices[I]) && infos == expected_swapchain &&
            alloc == &allocator,
        "shared swapchain routing and pointer identity");
  for (uint32_t i = 0; i < count; ++i)
    swapchains[i] = opaque<VkSwapchainKHR>(200 + I * 10 + i);
  return VK_SUCCESS;
}
template <int I>
void VKAPI_CALL DestroySwapchain(VkDevice device, VkSwapchainKHR,
                                 const VkAllocationCallbacks* alloc) {
  check(device == handle<VkDevice>(devices[I]) && alloc == &allocator,
        "swapchain destruction routing and allocator");
  ++swapchain_destroyed[I];
}
template <int I>
VkResult VKAPI_CALL Images(VkDevice device, VkSwapchainKHR, uint32_t* count,
                           VkImage* images) {
  check(device == handle<VkDevice>(devices[I]),
        "image discovery device routing");
  if (!images) {
    *count = 2;
    return VK_SUCCESS;
  }
  const uint32_t capacity = *count;
  for (uint32_t i = 0; i < std::min(capacity, 2U); ++i)
    images[i] = opaque<VkImage>(300 + i);
  *count = std::min(capacity, 2U);
  return capacity < 2 ? VK_INCOMPLETE : VK_SUCCESS;
}
template <int I>
VkResult VKAPI_CALL Present(VkQueue queue, const VkPresentInfoKHR* info) {
  bool own_queue = false;
  for (auto& object : queues[I]) own_queue |= handle<VkQueue>(object) == queue;
  bad_present |= !own_queue || info != expected_present;
  if (info->pResults) {
    for (uint32_t i = 0; i < info->swapchainCount; ++i)
      info->pResults[i] = VK_ERROR_OUT_OF_DATE_KHR;
  }
  ++forwarded;
  // Reenter procedure lookup: no registry/state mutex may be held downstream.
  bad_present |= layer_gdpa(handle<VkDevice>(devices[I]), "vkDeviceWaitIdle") !=
                 pointer(Unknown);
  return present_result;
}

template <int I>
PFN_vkVoidFunction VKAPI_CALL NextDevice(VkDevice device, const char* name) {
  check(device == handle<VkDevice>(devices[I]), "GDPA device routing");
#define RETURN(function, text) \
  if (std::strcmp(name, text) == 0) return pointer(function<I>)
  RETURN(DestroyDevice, "vkDestroyDevice");
  RETURN(GetQueue, "vkGetDeviceQueue");
  if constexpr (I == 0) {
    RETURN(GetQueue2, "vkGetDeviceQueue2");
  }
  RETURN(CreateSwapchain, "vkCreateSwapchainKHR");
  RETURN(CreateSharedSwapchains, "vkCreateSharedSwapchainsKHR");
  RETURN(DestroySwapchain, "vkDestroySwapchainKHR");
  RETURN(Images, "vkGetSwapchainImagesKHR");
  RETURN(Present, "vkQueuePresentKHR");
#undef RETURN
  if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
    return pointer(NextDevice<I>);
  if (std::strcmp(name, "vkDeviceWaitIdle") == 0) return pointer(Unknown);
  return nullptr;
}
template <int I>
PFN_vkVoidFunction VKAPI_CALL NextInstance(VkInstance instance,
                                           const char* name) {
  if (std::strcmp(name, "vkCreateInstance") == 0) {
    check(instance == VK_NULL_HANDLE,
          "create instance lookup uses NULL instance");
    return pointer(CreateInstance<I>);
  }
  check(instance == handle<VkInstance>(instances[I]),
        "GIPA instance routing, including CreateDevice");
#define RETURN(function, text) \
  if (std::strcmp(name, text) == 0) return pointer(function<I>)
  RETURN(DestroyInstance, "vkDestroyInstance");
  RETURN(PhysicalDevices, "vkEnumeratePhysicalDevices");
  RETURN(Groups, "vkEnumeratePhysicalDeviceGroups");
  RETURN(Groups, "vkEnumeratePhysicalDeviceGroupsKHR");
  RETURN(CreateDevice, "vkCreateDevice");
#undef RETURN
  if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0)
    return pointer(ExtensionProperties);
  if (std::strcmp(name, "vkInstanceUnknown") == 0) return pointer(Unknown);
  if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
    return pointer(NextDevice<I>);
  return NextDevice<I>(handle<VkDevice>(devices[I]), name);
}
PFN_vkVoidFunction VKAPI_CALL NextPhysical(VkInstance, const char* name) {
  return std::strcmp(name, "vkPhysicalUnknown") == 0 ? pointer(PhysicalUnknown)
                                                     : nullptr;
}

template <int I>
VkResult make_instance(VkInstance* instance) {
  VkLayerInstanceLink link{nullptr, NextInstance<I>,
                           I == 0 ? NextPhysical : nullptr};
  VkLayerInstanceCreateInfo chain{};
  chain.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
  chain.function = VK_LAYER_LINK_INFO;
  chain.u.pLayerInfo = &link;
  instance_chain = &chain;
  VkBaseInStructure extra{static_cast<VkStructureType>(123456),
                          reinterpret_cast<VkBaseInStructure*>(&chain)};
  VkInstanceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.pNext = &extra;
  expected_create = &info;
  return proc<PFN_vkCreateInstance>(VK_NULL_HANDLE, "vkCreateInstance")(
      &info, &allocator, instance);
}
template <int I>
VkResult make_device(VkDevice* device) {
  VkLayerDeviceLink link{nullptr, NextInstance<I>, NextDevice<I>};
  VkLayerDeviceCreateInfo chain{};
  chain.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
  chain.function = VK_LAYER_LINK_INFO;
  chain.u.pLayerInfo = &link;
  device_chain = &chain;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &chain;
  expected_create = &info;
  return proc<PFN_vkCreateDevice>(handle<VkInstance>(instances[I]),
                                  "vkCreateDevice")(
      handle<VkPhysicalDevice>(physicals[I]), &info, &allocator, device);
}

void device_checks(VkInstance instance, VkDevice device, int index) {
  auto get = reinterpret_cast<PFN_vkGetDeviceQueue>(
      layer_gdpa(device, "vkGetDeviceQueue"));
  std::array<VkQueue, 8> found{};
  for (uint32_t i = 0; i < found.size(); ++i) get(device, 7, i, &found[i]);
  if (index == 0) {
    VkDeviceQueueInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
    info.flags = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;
    info.queueFamilyIndex = 7;
    info.queueIndex = 1;
    VkQueue queue{};
    reinterpret_cast<PFN_vkGetDeviceQueue2>(
        layer_gdpa(device, "vkGetDeviceQueue2"))(device, &info, &queue);
    check(queue == found[1], "queue2 handle unchanged");
  } else
    check(!layer_gdpa(device, "vkGetDeviceQueue2"),
          "unsupported optional command stays NULL");
  check(!layer_gdpa(device, "vkNoSuchExtension"),
        "unknown unsupported device command stays NULL");
  auto create = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
      layer_gdpa(device, "vkCreateSwapchainKHR"));
  auto destroy = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
      layer_gdpa(device, "vkDestroySwapchainKHR"));
  auto images = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
      layer_gdpa(device, "vkGetSwapchainImagesKHR"));
  auto present = reinterpret_cast<PFN_vkQueuePresentKHR>(
      layer_gdpa(device, "vkQueuePresentKHR"));
  check(pointer(present) == layer_gipa(instance, "vkQueuePresentKHR"),
        "GIPA and GDPA return same intercept");
  VkBaseInStructure extra{static_cast<VkStructureType>(123457), nullptr};
  VkSwapchainCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.pNext = &extra;
  info.imageExtent = {640, 480};
  info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
  expected_swapchain = &info;
  VkSwapchainKHR swapchain{};
  swapchain_result = VK_ERROR_SURFACE_LOST_KHR;
  check(create(device, &info, &allocator, &swapchain) == swapchain_result,
        "swapchain error forwarded");
  swapchain_result = VK_SUCCESS;
  // Metadata OOM must not change a successful downstream creation.
  fail_allocation = 1;
  const auto created = create(device, &info, &allocator, &swapchain);
  fail_allocation = 0;
  check(created == VK_SUCCESS, "swapchain metadata OOM degrades to forwarding");
  destroy(device, swapchain, &allocator);
  check(create(device, &info, &allocator, &swapchain) == VK_SUCCESS,
        "swapchain creation forwarded");
  uint32_t count = 0;
  check(images(device, swapchain, &count, nullptr) == VK_SUCCESS && count == 2,
        "image count forwarded");
  std::array<VkImage, 2> output{};
  count = 1;
  check(images(device, swapchain, &count, output.data()) == VK_INCOMPLETE &&
            count == 1,
        "incomplete image list forwarded");
  count = 2;
  check(images(device, swapchain, &count, output.data()) == VK_SUCCESS &&
            count == 2,
        "full image list forwarded");
  check(output[0] == opaque<VkImage>(300) && output[1] == opaque<VkImage>(301),
        "image handles unchanged");
  std::array<VkSwapchainCreateInfoKHR, 2> infos{info, info};
  expected_swapchain = infos.data();
  std::array<VkSwapchainKHR, 2> shared{};
  auto create_shared = reinterpret_cast<PFN_vkCreateSharedSwapchainsKHR>(
      layer_gdpa(device, "vkCreateSharedSwapchainsKHR"));
  check(create_shared(device, 2, infos.data(), &allocator, shared.data()) ==
            VK_SUCCESS,
        "shared swapchain creation");
  for (auto swap : shared) destroy(device, swap, &allocator);
  std::array<VkResult, 2> results{};
  const std::array<VkSwapchainKHR, 2> swaps{swapchain, swapchain};
  const std::array<uint32_t, 2> indices{1, 0};
  const std::array<VkSemaphore, 2> waits{opaque<VkSemaphore>(400),
                                         opaque<VkSemaphore>(401)};
  VkPresentInfoKHR presentation{};
  presentation.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentation.pNext = &extra;
  presentation.waitSemaphoreCount = 2;
  presentation.pWaitSemaphores = waits.data();
  presentation.swapchainCount = 2;
  presentation.pSwapchains = swaps.data();
  presentation.pImageIndices = indices.data();
  presentation.pResults = results.data();
  expected_present = &presentation;
  fail_allocation = 1;
  const auto presented = present(found[0], &presentation);
  const bool allocated = fail_allocation != 1;
  fail_allocation = 0;
  check(!allocated && presented == VK_SUBOPTIMAL_KHR && !bad_present,
        "present allocates nothing and forwards exact pointer");
  check(results[0] == VK_ERROR_OUT_OF_DATE_KHR &&
            results[1] == VK_ERROR_OUT_OF_DATE_KHR,
        "per-swapchain results unchanged");
  presentation.pResults = nullptr;
  std::array<std::thread, 8> threads;
  std::atomic<bool> thread_failure{false};
  for (std::size_t i = 0; i < threads.size(); ++i) {
    threads[i] = std::thread([&, i] {
      expected_present = &presentation;
      for (int frame = 0; frame < 1000; ++frame)
        if (present(found[i], &presentation) != VK_SUBOPTIMAL_KHR)
          thread_failure = true;
      if (bad_present) thread_failure = true;
    });
  }
  for (auto& thread : threads) thread.join();
  check(!thread_failure, "concurrent queues forward safely");
  destroy(device, swapchain, &allocator);
  check(swapchain_destroyed[index] == 4, "all swapchain destroys forwarded");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  unsetenv("GLYPHSHIFT_LOG");
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    std::cerr << dlerror() << '\n';
    return 1;
  }
  auto negotiate = reinterpret_cast<PFN_vkNegotiateLoaderLayerInterfaceVersion>(
      dlsym(library, "vkNegotiateLoaderLayerInterfaceVersion"));
  if (!negotiate) return 1;
  VkNegotiateLayerInterface negotiation{};
  check(negotiate(nullptr) == VK_ERROR_INITIALIZATION_FAILED,
        "null negotiation refused");
  negotiation.sType = LAYER_NEGOTIATE_INTERFACE_STRUCT;
  negotiation.loaderLayerInterfaceVersion = 1;
  check(negotiate(&negotiation) == VK_ERROR_INITIALIZATION_FAILED,
        "legacy interface refused");
  negotiation.loaderLayerInterfaceVersion = 99;
  check(negotiate(&negotiation) == VK_SUCCESS &&
            negotiation.loaderLayerInterfaceVersion == 2,
        "newer loader negotiates supported interface 2");
  layer_gipa = negotiation.pfnGetInstanceProcAddr;
  layer_gdpa = negotiation.pfnGetDeviceProcAddr;
  check(layer_gipa && layer_gdpa && negotiation.pfnGetPhysicalDeviceProcAddr,
        "all negotiated functions supplied");
  check(!layer_gipa(VK_NULL_HANDLE, "vkQueuePresentKHR"),
        "device intercept not exposed globally");
  check(!layer_gipa(VK_NULL_HANDLE, nullptr) &&
            !layer_gdpa(VK_NULL_HANDLE, "vkNoSuch"),
        "null lookup guarded");
  VkInstanceCreateInfo missing_chain{};
  missing_chain.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  VkInstance missing_instance{};
  check(proc<PFN_vkCreateInstance>(VK_NULL_HANDLE, "vkCreateInstance")(
            &missing_chain, &allocator, &missing_instance) ==
            VK_ERROR_INITIALIZATION_FAILED,
        "missing loader link fails safely");
  auto layer_properties = proc<PFN_vkEnumerateInstanceLayerProperties>(
      VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties");
  uint32_t layer_count = 0;
  VkLayerProperties properties{};
  check(
      layer_properties(&layer_count, nullptr) == VK_SUCCESS && layer_count == 1,
      "own layer count");
  layer_count = 0;
  check(layer_properties(&layer_count, &properties) == VK_INCOMPLETE,
        "zero layer capacity");
  layer_count = 1;
  check(
      layer_properties(&layer_count, &properties) == VK_SUCCESS &&
          std::strcmp(properties.layerName, "VK_LAYER_GLYPHSHIFT_overlay") == 0,
      "own layer property enumeration");
  auto layer_extensions = proc<PFN_vkEnumerateInstanceExtensionProperties>(
      VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
  check(layer_extensions(nullptr, &layer_count, nullptr) ==
            VK_ERROR_LAYER_NOT_PRESENT,
        "instance extensions do not claim driver extensions");
  check(layer_extensions("VK_LAYER_GLYPHSHIFT_overlay", &layer_count,
                         nullptr) == VK_SUCCESS &&
            layer_count == 0,
        "own layer has no instance extensions");
  VkInstance first{}, second{};
  fail_allocation = 1;
  auto result = make_instance<0>(&first);
  fail_allocation = 0;
  check(result == VK_ERROR_OUT_OF_HOST_MEMORY && instance_creates == 0,
        "instance allocation failure before downstream create");
  create_result = VK_ERROR_INCOMPATIBLE_DRIVER;
  check(make_instance<0>(&first) == create_result,
        "instance driver failure unchanged");
  create_result = VK_SUCCESS;
  fail_allocation = 2;
  result = make_instance<0>(&first);
  fail_allocation = 0;
  check(result == VK_ERROR_OUT_OF_HOST_MEMORY && !first &&
            instance_destroyed[0] == 1,
        "instance registry OOM rolls back downstream instance");
  check(make_instance<0>(&first) == VK_SUCCESS &&
            make_instance<1>(&second) == VK_SUCCESS,
        "multiple instances created");
  std::array<VkInstance, 2> instance_handles{first, second};
  std::array<VkDevice, 2> device_handles{};
  for (int i = 0; i < 2; ++i) {
    for (auto& queue : queues[i]) queue.dispatch = &device_tokens[i];
    const auto instance = instance_handles[i];
    check(layer_gipa(instance, "vkInstanceUnknown") == pointer(Unknown),
          "unknown instance command forwarded");
    check(negotiation.pfnGetPhysicalDeviceProcAddr(instance,
                                                   "vkPhysicalUnknown") ==
              (i == 0 ? pointer(PhysicalUnknown) : nullptr),
          "unknown physical commands only use the physical chain");
    check(
        !negotiation.pfnGetPhysicalDeviceProcAddr(instance, "vkDeviceWaitIdle"),
        "device command is never misclassified as a physical command");
    check(
        negotiation.pfnGetPhysicalDeviceProcAddr(instance, "vkCreateDevice") ==
            layer_gipa(instance, "vkCreateDevice"),
        "known physical intercept works without a downstream physical hook");
    check(!negotiation.pfnGetPhysicalDeviceProcAddr(instance, "vkAbsent"),
          "unknown physical NULL preserved");
    uint32_t count = 0;
    auto enumerate = proc<PFN_vkEnumeratePhysicalDevices>(
        instance, "vkEnumeratePhysicalDevices");
    check(enumerate(instance, &count, nullptr) == VK_SUCCESS && count == 1,
          "physical count query forwarded");
    VkPhysicalDevice physical{};
    check(enumerate(instance, &count, &physical) == VK_INCOMPLETE &&
              physical == handle<VkPhysicalDevice>(physicals[i]),
          "physical handles/results unchanged");
    for (const char* name : {"vkEnumeratePhysicalDeviceGroups",
                             "vkEnumeratePhysicalDeviceGroupsKHR"}) {
      VkPhysicalDeviceGroupProperties group{};
      count = 1;
      check(proc<PFN_vkEnumeratePhysicalDeviceGroups>(instance, name)(
                instance, &count, &group) == VK_SUCCESS &&
                group.physicalDevices[0] == physical,
            "physical device groups and KHR alias");
    }
    auto extensions = proc<PFN_vkEnumerateDeviceExtensionProperties>(
        instance, "vkEnumerateDeviceExtensionProperties");
    count = 99;
    check(extensions(physical, "VK_LAYER_GLYPHSHIFT_overlay", &count,
                     nullptr) == VK_SUCCESS &&
              count == 0,
          "own layer exposes no extensions");
    check(extensions(physical, nullptr, &count, nullptr) == VK_INCOMPLETE &&
              count == 3,
          "driver extension enumeration forwarded");
  }
  create_result = VK_ERROR_FEATURE_NOT_PRESENT;
  check(make_device<0>(&device_handles[0]) == create_result,
        "device driver failure unchanged");
  create_result = VK_SUCCESS;
  fail_allocation = 2;
  result = make_device<0>(&device_handles[0]);
  fail_allocation = 0;
  check(result == VK_ERROR_OUT_OF_HOST_MEMORY && !device_handles[0] &&
            device_destroyed[0] == 1,
        "device registry OOM rolls back downstream device");
  check(make_device<0>(&device_handles[0]) == VK_SUCCESS &&
            make_device<1>(&device_handles[1]) == VK_SUCCESS,
        "multiple devices created");
  for (int i = 0; i < 2; ++i)
    device_checks(instance_handles[i], device_handles[i], i);
  check(forwarded == 16002,
        "all presents forwarded exactly once across both devices");
  for (int i = 0; i < 2; ++i) {
    proc<PFN_vkDestroyDevice>(instance_handles[i], "vkDestroyDevice")(
        device_handles[i], &allocator);
    check(!layer_gdpa(device_handles[i], "vkQueuePresentKHR"),
          "destroy removes device registry entry");
    proc<PFN_vkDestroyInstance>(instance_handles[i], "vkDestroyInstance")(
        instance_handles[i], &allocator);
    check(!layer_gipa(instance_handles[i], "vkInstanceUnknown"),
          "destroy removes instance registry entry");
  }
  check(instance_destroyed[0] == 2 && instance_destroyed[1] == 1 &&
            device_destroyed[0] == 2 && device_destroyed[1] == 1,
        "all instance/device resources destroyed exactly once including "
        "rollback");
  dlclose(library);
  if (!failures)
    std::cout
        << "Vulkan negotiation, chain routing, lifetime, OOM, optional/unknown "
           "commands and concurrent presentation passed\n";
  return failures ? 1 : 0;
}
