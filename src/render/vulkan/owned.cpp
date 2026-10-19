#include "owned.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace glyphshift::vulkan {
namespace {
// Validation findings, and the loader's own messages labeled apart (they
// include notes on layers disabled by VK_LOADER_LAYERS_DISABLE).
VKAPI_ATTR VkBool32 VKAPI_CALL report(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::fprintf(stderr,
            "%s: %s\n",
            type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ? "Vulkan validation"
                                                                  : "Vulkan loader",
            data->pMessage);
    return VK_FALSE;
}

bool validation_requested() {
    const char* value = std::getenv("GLYPHSHIFT_VULKAN_VALIDATION");
    return value && std::strcmp(value, "1") == 0;
}

bool has_extensions(VkPhysicalDevice physical, const std::vector<const char*>& wanted) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, available.data());
    return std::all_of(wanted.begin(), wanted.end(), [&](const char* name) {
        return std::any_of(available.begin(), available.end(), [&](const auto& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
    });
}
}  // namespace

OwnedRenderer::OwnedRenderer(const Options& options,
    const std::function<VkSurfaceKHR(VkInstance)>& make_surface) {
    try {
        const bool validation = validation_requested();
        auto extensions = options.instance_extensions;
        if (validation)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        auto application = info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        application.pApplicationName = "glyphshift-overlay";
        application.apiVersion =
            options.timeline_semaphores ? VK_API_VERSION_1_2 : VK_API_VERSION_1_0;
        const char* layer = "VK_LAYER_KHRONOS_validation";
        const VkValidationFeatureEnableEXT synchronization =
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        auto features = info<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = &synchronization;
        auto messenger = info<VkDebugUtilsMessengerCreateInfoEXT>(
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
        messenger.pNext = &features;
        messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        messenger.pfnUserCallback = report;
        auto create = info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        create.pApplicationInfo = &application;
        create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        if (validation) {
            create.enabledLayerCount = 1;
            create.ppEnabledLayerNames = &layer;
            create.pNext = &messenger;
        }
        check(vkCreateInstance(&create, nullptr, &instance_), "create Vulkan instance");
        if (validation) {
            const auto make = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
            if (!make)
                throw std::runtime_error("VK_EXT_debug_utils is unavailable");
            messenger.pNext = nullptr;
            check(make(instance_, &messenger, nullptr, &messenger_), "create debug messenger");
        }
        surface_ = make_surface(instance_);
        select_device(options);
        api_ = load_render_api(vkGetDeviceProcAddr, device_);
        if (!api_.available)
            throw std::runtime_error("incomplete Vulkan device functions");
        resources_ = std::make_unique<Resources>(Device{device_,
            physical_,
            &api_,
            vkGetPhysicalDeviceMemoryProperties,
            vkGetPhysicalDeviceFormatProperties});
        auto pool = info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = family_;
        check(vkCreateCommandPool(device_, &pool, nullptr, &pool_), "create command pool");
        auto allocation =
            info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        allocation.commandPool = pool_;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        auto semaphore = info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        auto fence = info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (auto& frame : frames_) {
            check(vkAllocateCommandBuffers(device_, &allocation, &frame.command),
                "allocate command buffer");
            check(vkCreateSemaphore(device_, &semaphore, nullptr, &frame.acquired),
                "create semaphore");
            check(vkCreateFence(device_, &fence, nullptr, &frame.fence), "create fence");
        }
    } catch (...) {
        release();
        throw;
    }
}

OwnedRenderer::~OwnedRenderer() {
    release();
}

void OwnedRenderer::release() noexcept {
    if (device_) {
        vkDeviceWaitIdle(device_);
        destroy_swapchain();
        for (auto& frame : frames_) {
            if (frame.fence)
                vkDestroyFence(device_, frame.fence, nullptr);
            if (frame.acquired)
                vkDestroySemaphore(device_, frame.acquired, nullptr);
            frame = {};
        }
        if (pool_)
            vkDestroyCommandPool(device_, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
        resources_.reset();
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_)
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (messenger_) {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy)
            destroy(instance_, messenger_, nullptr);
        messenger_ = VK_NULL_HANDLE;
    }
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
}

// The first GPU (discrete first) with a queue family that draws and presents
// to the surface and every requested device extension.
void OwnedRenderer::select_device(const Options& options) {
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "enumerate GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "enumerate GPUs");
    auto device_extensions = options.device_extensions;
    device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    for (const bool discrete : {true, false})
        for (const auto candidate : devices) {
            if (physical_)
                break;
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if ((properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) != discrete ||
                !has_extensions(candidate, device_extensions) ||
                (options.timeline_semaphores && properties.apiVersion < VK_API_VERSION_1_2))
                continue;
            uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> list(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, list.data());
            for (uint32_t i = 0; i < families && !physical_; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present);
                if ((list[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    physical_ = candidate;
                    family_ = i;
                }
            }
        }
    if (!physical_)
        throw std::runtime_error("no Vulkan GPU can render and present to the overlay window");
    auto ids = info<VkPhysicalDeviceIDProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
    auto properties =
        info<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    properties.pNext = &ids;
    vkGetPhysicalDeviceProperties2(physical_, &properties);
    if (ids.deviceLUIDValid)
        std::memcpy(luid_.data(), ids.deviceLUID, luid_.size());
    const auto api = properties.properties.apiVersion;
    version_ = "Vulkan " + std::to_string(VK_API_VERSION_MAJOR(api)) + "." +
               std::to_string(VK_API_VERSION_MINOR(api)) + "." +
               std::to_string(VK_API_VERSION_PATCH(api)) + ", " + properties.properties.deviceName;
    const float priority = 1;
    auto queue = info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    queue.queueFamilyIndex = family_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    auto vulkan12 = info<VkPhysicalDeviceVulkan12Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    vulkan12.timelineSemaphore = VK_TRUE;
    auto create = info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    if (options.timeline_semaphores)
        create.pNext = &vulkan12;
    create.queueCreateInfoCount = 1;
    create.pQueueCreateInfos = &queue;
    create.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
    create.ppEnabledExtensionNames = device_extensions.data();
    check(vkCreateDevice(physical_, &create, nullptr, &device_), "create Vulkan device");
    vkGetDeviceQueue(device_, family_, 0, &queue_);
}

void OwnedRenderer::destroy_swapchain() noexcept {
    pipeline_.reset();
    bound_source_ = VK_NULL_HANDLE;
    for (const auto view : views_)
        vkDestroyImageView(device_, view, nullptr);
    for (const auto semaphore : rendered_)
        vkDestroySemaphore(device_, semaphore, nullptr);
    views_.clear();
    rendered_.clear();
    images_.clear();
    if (swapchain_)
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void OwnedRenderer::recreate(VkExtent2D extent) {
    check(vkDeviceWaitIdle(device_), "wait for the device");
    destroy_swapchain();
    stale_ = false;
    VkSurfaceCapabilitiesKHR capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &capabilities),
        "query surface capabilities");
    extent_ = capabilities.currentExtent.width != UINT32_MAX ? capabilities.currentExtent : extent;
    if (!extent_.width || !extent_.height)
        return;
    uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr),
        "query surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data()),
        "query surface formats");
    const auto chosen = std::find_if(formats.begin(), formats.end(), [](const auto& format) {
        return (format.format == VK_FORMAT_B8G8R8A8_UNORM ||
                   format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
               format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (chosen == formats.end())
        throw std::runtime_error("the overlay surface has no 8-bit UNORM format");
    format_ = chosen->format;
    auto create = info<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    create.surface = surface_;
    create.minImageCount = std::max(capabilities.minImageCount + 1, 2U);
    if (capabilities.maxImageCount)
        create.minImageCount = std::min(create.minImageCount, capabilities.maxImageCount);
    create.imageFormat = format_;
    create.imageColorSpace = chosen->colorSpace;
    create.imageExtent = extent_;
    create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.preTransform = capabilities.currentTransform;
    create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // Presented on vblank, like the X11 overlay's swap interval 1.
    create.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    create.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &create, nullptr, &swapchain_), "create swapchain");
    check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr), "query swapchain images");
    images_.resize(count);
    check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data()),
        "query swapchain images");
    auto semaphore = info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    for (const auto image : images_) {
        views_.push_back(resources_->image_view(image, format_));
        rendered_.push_back(VK_NULL_HANDLE);
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &rendered_.back()),
            "create present semaphore");
    }
    pipeline_ = std::make_unique<Pipeline>(*resources_,
        format_,
        extent_,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        static_cast<unsigned>(images_.size()),
        selection_);
}

bool OwnedRenderer::draw(ExternalSource& source,
    VkExtent2D extent,
    const Settings& settings,
    const ImDrawData* ui) {
    if (!extent.width || !extent.height)
        return false;
    if (pipeline_ && !pipeline_->matches(settings.glyphs))
        stale_ = true;
    if (!swapchain_ || stale_ || extent.width != extent_.width || extent.height != extent_.height) {
        selection_ = settings.glyphs;
        recreate(extent);
    }
    const auto size = source.extent();
    if (!swapchain_ || size.width != extent_.width || size.height != extent_.height)
        return false;
    if (source.view() != bound_source_) {
        check(vkDeviceWaitIdle(device_), "wait for the device");
        for (unsigned i = 0; i < views_.size(); ++i)
            pipeline_->set_target(i, source.view(), views_[i]);
        bound_source_ = source.view();
    }
    auto& frame = frames_[frame_ % frames_.size()];
    check(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX), "wait for a frame");
    uint32_t index = 0;
    const auto acquired = vkAcquireNextImageKHR(device_,
        swapchain_,
        UINT64_MAX,
        frame.acquired,
        VK_NULL_HANDLE,
        &index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        stale_ = true;
        return false;
    }
    if (acquired != VK_SUBOPTIMAL_KHR)
        check(acquired, "acquire a swapchain image");
    check(vkResetCommandBuffer(frame.command, 0), "reset command buffer");
    auto begin = info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame.command, &begin), "begin commands");
    source.acquire(frame.command);
    resources_->barrier(frame.command,
        images_[index],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (settings.enabled)
        pipeline_->record(frame.command, index, settings, ui);
    else {
        // F10 with the editor open: the original image under the panel.
        auto original = settings;
        original.enabled = true;
        original.art.illustrated = false;
        original.source_color = true;
        original.glyph_strength = 0;
        pipeline_->record(frame.command, index, original, ui);
    }
    source.release(frame.command);
    check(vkEndCommandBuffer(frame.command), "end commands");
    // The source's values are reserved only now that the frame is submitted.
    const auto use = source.use();
    const std::array<VkSemaphore, 2> waits{frame.acquired, source.semaphore()};
    const std::array<VkPipelineStageFlags, 2> stages{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
    const std::array<uint64_t, 2> wait_values{0, use.wait};
    const std::array<VkSemaphore, 2> signals{rendered_[index], source.semaphore()};
    const std::array<uint64_t, 2> signal_values{0, use.signal};
    auto timeline =
        info<VkTimelineSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO);
    timeline.waitSemaphoreValueCount = 2;
    timeline.pWaitSemaphoreValues = wait_values.data();
    timeline.signalSemaphoreValueCount = 2;
    timeline.pSignalSemaphoreValues = signal_values.data();
    const bool shared = source.semaphore() != VK_NULL_HANDLE;
    auto submit = info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    if (shared)
        submit.pNext = &timeline;
    submit.waitSemaphoreCount = shared ? 2 : 1;
    submit.pWaitSemaphores = waits.data();
    submit.pWaitDstStageMask = stages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.command;
    submit.signalSemaphoreCount = shared ? 2 : 1;
    submit.pSignalSemaphores = signals.data();
    check(vkResetFences(device_, 1, &frame.fence), "reset fence");
    check(vkQueueSubmit(queue_, 1, &submit, frame.fence), "submit frame");
    auto present = info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &rendered_[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &index;
    const auto presented = vkQueuePresentKHR(queue_, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR ||
        acquired == VK_SUBOPTIMAL_KHR)
        stale_ = true;
    else
        check(presented, "present");
    ++frame_;
    return true;
}
}  // namespace glyphshift::vulkan
