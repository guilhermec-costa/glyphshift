#include <vulkan/vulkan.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glyphshift/config/settings.hpp>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/render/vulkan/pipeline.hpp"
#include "ppm.hpp"

// Developer-only, real GPU preview on fixed images through the Vulkan glyph
// pipeline, offscreen (no window, no surface). Same command line and files as
// the OpenGL preview, so the image oracles run against either renderer.
// GLYPHSHIFT_PREVIEW_VALIDATION=1 enables Khronos validation (with
// synchronization checks); any warning or error then fails the preview.
namespace {
using namespace glyphshift::vulkan;

int validation_messages = 0;

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

VKAPI_ATTR VkBool32 VKAPI_CALL report(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        ++validation_messages;
        std::cerr << "Vulkan validation: " << data->pMessage << '\n';
    }
    return VK_FALSE;
}

// Instance, device and queue of the first GPU with a graphics queue,
// discrete GPUs first.
struct Context {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    std::string name;

    explicit Context(bool validation) {
        auto app = info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        app.pApplicationName = "glyphshift-preview";
        app.apiVersion = VK_API_VERSION_1_0;
        const char* layer = "VK_LAYER_KHRONOS_validation";
        const char* extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        const VkValidationFeatureEnableEXT synchronization =
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        auto features = info<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = &synchronization;
        auto create = info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        create.pApplicationInfo = &app;
        if (validation) {
            create.pNext = &features;
            create.enabledLayerCount = 1;
            create.ppEnabledLayerNames = &layer;
            create.enabledExtensionCount = 1;
            create.ppEnabledExtensionNames = &extension;
        }
        check(vkCreateInstance(&create, nullptr, &instance),
            validation ? "create Vulkan instance with validation" : "create Vulkan instance");
        if (validation) {
            auto messenger_info = info<VkDebugUtilsMessengerCreateInfoEXT>(
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
            messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
            messenger_info.pfnUserCallback = report;
            const auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            require(create_messenger, "no debug messenger");
            check(create_messenger(instance, &messenger_info, nullptr, &messenger),
                "create debug messenger");
        }
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate GPUs");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate GPUs");
        for (const bool discrete : {true, false})
            for (const auto candidate : devices) {
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(candidate, &properties);
                if (physical ||
                    (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) != discrete)
                    continue;
                uint32_t families = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
                std::vector<VkQueueFamilyProperties> list(families);
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, list.data());
                for (uint32_t i = 0; i < families && !physical; ++i)
                    if (list[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                        physical = candidate;
                        family = i;
                        name = properties.deviceName;
                    }
            }
        require(physical, "No Vulkan GPU with a graphics queue");
        const float priority = 1;
        auto queue_info = info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        auto device_info = info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        check(vkCreateDevice(physical, &device_info, nullptr, &device), "create Vulkan device");
        vkGetDeviceQueue(device, family, 0, &queue);
    }

    ~Context() {
        if (device)
            vkDestroyDevice(device, nullptr);
        if (messenger)
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,
                "vkDestroyDebugUtilsMessengerEXT"))(instance, messenger, nullptr);
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }
};

// A host-visible, coherent buffer kept mapped.
struct HostBuffer {
    const Resources& resources;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    unsigned char* mapped = nullptr;

    HostBuffer(const Resources& owner, VkDeviceSize size, VkBufferUsageFlags usage)
        : resources(owner) {
        const auto& api = resources.api();
        const auto device = resources.device().handle;
        auto create = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        create.size = size;
        create.usage = usage;
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(api.CreateBuffer(device, &create, nullptr, &buffer), "create preview buffer");
        VkMemoryRequirements requirements{};
        api.GetBufferMemoryRequirements(device, buffer, &requirements);
        resources.allocate(requirements,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            memory);
        check(api.BindBufferMemory(device, buffer, memory, 0), "bind preview buffer");
        void* pointer = nullptr;
        check(api.MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &pointer), "map preview buffer");
        mapped = static_cast<unsigned char*>(pointer);
    }

    ~HostBuffer() {
        const auto& api = resources.api();
        const auto device = resources.device().handle;
        if (mapped)
            api.UnmapMemory(device, memory);
        if (buffer)
            api.DestroyBuffer(device, buffer, nullptr);
        if (memory)
            api.FreeMemory(device, memory, nullptr);
    }

    HostBuffer(const HostBuffer&) = delete;
    HostBuffer& operator=(const HostBuffer&) = delete;
};
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc % 2) {
        std::cerr << "Usage: glyphshift-preview INPUT.ppm SETTINGS.conf OUTPUT.ppm "
                     "[NEXT_INPUT.ppm NEXT_OUTPUT.ppm ...]\n";
        return 2;
    }
    const char* validation_variable = std::getenv("GLYPHSHIFT_PREVIEW_VALIDATION");
    const bool validation = validation_variable && std::strcmp(validation_variable, "1") == 0;
    try {
        unsigned width = 0, height = 0;
        auto pixels = glyphshift::tools::read_ppm(argv[1], width, height);
        std::ifstream config(argv[2]);
        require(static_cast<bool>(config), "Cannot read settings");
        const auto parsed = glyphshift::parse_settings(
            std::string(std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()));
        require(!parsed.warnings, "Invalid settings");
        const auto& settings = parsed.value;
        {
            const Context context(validation);
            const RenderApi api = load_render_api(vkGetDeviceProcAddr, context.device);
            require(api.available, "Incomplete Vulkan device functions");
            const Resources resources(Device{context.device,
                context.physical,
                &api,
                vkGetPhysicalDeviceMemoryProperties,
                vkGetPhysicalDeviceFormatProperties});
            const auto device = context.device;
            constexpr VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
            const VkExtent2D extent{width, height};
            Texture source, output;
            resources.texture(source, format, extent);
            resources.texture(output,
                format,
                extent,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            const VkDeviceSize bytes = VkDeviceSize{width} * height * 4;
            HostBuffer upload(resources, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            HostBuffer readback(resources, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            VkCommandPool pool = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            {
                Pipeline pipeline(resources,
                    format,
                    extent,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    1,
                    settings.glyphs);
                // Frames of a sequence are 1/60 s apart, so exposure adaptation
                // is reproducible.
                pipeline.set_frame_interval(1.0 / 60);
                pipeline.set_target(0, source.view, output.view);
                auto pool_info =
                    info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
                pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                pool_info.queueFamilyIndex = context.family;
                check(api.CreateCommandPool(device, &pool_info, nullptr, &pool),
                    "create command pool");
                auto allocation = info<VkCommandBufferAllocateInfo>(
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
                allocation.commandPool = pool;
                allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocation.commandBufferCount = 1;
                VkCommandBuffer command = VK_NULL_HANDLE;
                check(api.AllocateCommandBuffers(device, &allocation, &command),
                    "allocate command buffer");
                auto fence_info = info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
                check(api.CreateFence(device, &fence_info, nullptr, &fence), "create fence");
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {width, height, 1};
                for (int frame = 0; 2 * frame + 3 < argc; ++frame) {
                    if (frame) {
                        unsigned next_width = 0, next_height = 0;
                        pixels = glyphshift::tools::read_ppm(argv[2 * frame + 2],
                            next_width,
                            next_height);
                        require(next_width == width && next_height == height,
                            "Sequence frames must share dimensions");
                    }
                    if (settings.enabled) {
                        // Rows stay top-down: Vulkan samples the source that way.
                        for (std::size_t i = 0, count = std::size_t{width} * height; i < count;
                            ++i) {
                            std::memcpy(upload.mapped + 4 * i, pixels.data() + 3 * i, 3);
                            upload.mapped[4 * i + 3] = 255;
                        }
                        check(api.ResetCommandBuffer(command, 0), "reset command buffer");
                        auto begin = info<VkCommandBufferBeginInfo>(
                            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
                        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                        check(api.BeginCommandBuffer(command, &begin), "begin commands");
                        resources.barrier(command,
                            source.image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT);
                        api.CmdCopyBufferToImage(command,
                            upload.buffer,
                            source.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &region);
                        resources.barrier(command,
                            source.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                        resources.barrier(command,
                            output.image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_TRANSFER_READ_BIT,
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
                        pipeline.record(command, 0, settings, nullptr);
                        // The pipeline leaves the output as a color attachment
                        // (no transition at the end of its render pass); this
                        // barrier orders the copy after its color writes.
                        resources.barrier(command,
                            output.image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_TRANSFER_READ_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT);
                        vkCmdCopyImageToBuffer(command,
                            output.image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            readback.buffer,
                            1,
                            &region);
                        auto host =
                            info<VkBufferMemoryBarrier>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
                        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex =
                            VK_QUEUE_FAMILY_IGNORED;
                        host.buffer = readback.buffer;
                        host.size = VK_WHOLE_SIZE;
                        api.CmdPipelineBarrier(command,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_HOST_BIT,
                            0,
                            0,
                            nullptr,
                            1,
                            &host,
                            0,
                            nullptr);
                        check(api.EndCommandBuffer(command), "end commands");
                        auto submit = info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
                        submit.commandBufferCount = 1;
                        submit.pCommandBuffers = &command;
                        check(api.QueueSubmit(context.queue, 1, &submit, fence), "submit frame");
                        check(api.WaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX),
                            "wait for frame");
                        check(api.ResetFences(device, 1, &fence), "reset fence");
                        for (std::size_t i = 0, count = std::size_t{width} * height; i < count; ++i)
                            std::memcpy(pixels.data() + 3 * i, readback.mapped + 4 * i, 3);
                    }
                    glyphshift::tools::write_ppm(argv[2 * frame + 3], pixels, width, height);
                }
                check(api.QueueWaitIdle(context.queue), "wait for the queue");
            }
            api.DestroyFence(device, fence, nullptr);
            api.DestroyCommandPool(device, pool, nullptr);
            resources.destroy(output);
            resources.destroy(source);
            std::cout << "Preview " << width << 'x' << height
                      << " strength=" << settings.glyph_strength << " frames=" << (argc - 2) / 2
                      << " (Vulkan, " << context.name << ")\n";
        }
        if (validation_messages) {
            std::cerr << validation_messages << " Vulkan validation message(s)\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
