#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "window.hpp"

namespace {
void vk_check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(std::string(operation) + " failed: VkResult " +
                             std::to_string(result));
}
template <class T>
T info(VkStructureType type) {
  T value{};
  value.sType = type;
  return value;
}
std::atomic<unsigned> validation_errors{0};
VKAPI_ATTR VkBool32 VKAPI_CALL
diagnostic(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
           VkDebugUtilsMessageTypeFlagsEXT type,
           const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  const bool validation =
      type & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);
  std::cerr << (validation ? "Vulkan validation: " : "Vulkan loader: ")
            << data->pMessage << '\n';
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    ++validation_errors;
  return VK_FALSE;
}
struct Frame {
  VkCommandBuffer command = VK_NULL_HANDLE;
  VkSemaphore acquired = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
};
class Renderer {
 public:
  fixture::Window window;
  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t family = 0;
  VkCommandPool pool = VK_NULL_HANDLE;
  std::array<Frame, 2> frames{};
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkExtent2D extent{};
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  std::array<VkShaderModule, 2> shaders{};
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> framebuffers;
  // Present can outlive a submission fence; use one signal semaphore per
  // swapchain image, reused only once that same image has been reacquired.
  std::vector<VkSemaphore> presented;

  ~Renderer() {
    if (device) {
      vkDeviceWaitIdle(device);
      destroy_swapchain();
      for (auto& frame : frames) {
        if (frame.fence) vkDestroyFence(device, frame.fence, nullptr);
        if (frame.acquired) vkDestroySemaphore(device, frame.acquired, nullptr);
      }
      if (pool) vkDestroyCommandPool(device, pool, nullptr);
      for (const auto shader : shaders)
        if (shader) vkDestroyShaderModule(device, shader, nullptr);
      if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
      vkDestroyDevice(device, nullptr);
    }
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (messenger) {
      auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
      if (destroy) destroy(instance, messenger, nullptr);
    }
    // Close X11 before unloading the ICD: NVIDIA registers Xlib close
    // callbacks whose code must still be loaded when XCloseDisplay runs.
    window.close();
    if (instance) vkDestroyInstance(instance, nullptr);
  }
  void init(bool validation) {
    XVisualInfo visual_template{};
    visual_template.visualid = XVisualIDFromVisual(
        DefaultVisual(window.display, DefaultScreen(window.display)));
    int count = 0;
    std::unique_ptr<XVisualInfo, decltype(&XFree)> visual(
        XGetVisualInfo(window.display, VisualIDMask, &visual_template, &count),
        XFree);
    if (!visual || count == 0)
      throw std::runtime_error("Cannot get X11 visual");
    window.create(visual.get(), "GlyphShift Vulkan fixture");
    const char* layer = "VK_LAYER_KHRONOS_validation";
    std::vector<const char*> extensions{VK_KHR_SURFACE_EXTENSION_NAME,
                                        VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
    auto debug = info<VkDebugUtilsMessengerCreateInfoEXT>(
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = diagnostic;
    if (validation) {
      std::uint32_t n = 0;
      vk_check(vkEnumerateInstanceLayerProperties(&n, nullptr),
               "enumerate layers");
      std::vector<VkLayerProperties> layers(n);
      vk_check(vkEnumerateInstanceLayerProperties(&n, layers.data()),
               "enumerate layers");
      if (std::none_of(layers.begin(), layers.end(), [&](const auto& p) {
            return std::strcmp(p.layerName, layer) == 0;
          }))
        throw std::runtime_error(
            "--validation requires VK_LAYER_KHRONOS_validation; install Vulkan "
            "validation layers");
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    auto application =
        info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    application.pApplicationName = "GlyphShift M0 fixture";
    application.apiVersion = VK_API_VERSION_1_0;
    auto create =
        info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    create.pApplicationInfo = &application;
    create.enabledExtensionCount =
        static_cast<std::uint32_t>(extensions.size());
    create.ppEnabledExtensionNames = extensions.data();
    if (validation) {
      create.enabledLayerCount = 1;
      create.ppEnabledLayerNames = &layer;
      create.pNext = &debug;
    }
    vk_check(vkCreateInstance(&create, nullptr, &instance), "create instance");
    if (validation) {
      auto make = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
      if (!make) throw std::runtime_error("VK_EXT_debug_utils is unavailable");
      vk_check(make(instance, &debug, nullptr, &messenger),
               "create debug messenger");
    }
    auto surface_info = info<VkXlibSurfaceCreateInfoKHR>(
        VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR);
    surface_info.dpy = window.display;
    surface_info.window = window.handle;
    vk_check(vkCreateXlibSurfaceKHR(instance, &surface_info, nullptr, &surface),
             "create Xlib surface");
    select_device();
    auto command_pool = info<VkCommandPoolCreateInfo>(
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    command_pool.queueFamilyIndex = family;
    command_pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vk_check(vkCreateCommandPool(device, &command_pool, nullptr, &pool),
             "create command pool");
    auto allocation = info<VkCommandBufferAllocateInfo>(
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocation.commandPool = pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    const auto semaphore =
        info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    auto fence = info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (auto& frame : frames) {
      vk_check(vkAllocateCommandBuffers(device, &allocation, &frame.command),
               "allocate command buffer");
      vk_check(vkCreateSemaphore(device, &semaphore, nullptr, &frame.acquired),
               "create acquire semaphore");
      vk_check(vkCreateFence(device, &fence, nullptr, &frame.fence),
               "create fence");
    }
    const std::array<const char*, 2> paths{"/gradient.vert.spv",
                                           "/gradient.frag.spv"};
    for (std::size_t i = 0; i < shaders.size(); ++i) {
      const auto code =
          fixture::read_spirv(std::string(FIXTURE_SHADER_DIR) + paths[i]);
      auto module = info<VkShaderModuleCreateInfo>(
          VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
      module.codeSize = code.size() * sizeof(std::uint32_t);
      module.pCode = code.data();
      vk_check(vkCreateShaderModule(device, &module, nullptr, &shaders[i]),
               "create shader module");
    }
    VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                              3 * sizeof(float)};
    auto pipeline_layout = info<VkPipelineLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pipeline_layout.pushConstantRangeCount = 1;
    pipeline_layout.pPushConstantRanges = &range;
    vk_check(vkCreatePipelineLayout(device, &pipeline_layout, nullptr, &layout),
             "create pipeline layout");
    recreate();
  }
  void select_device() {
    std::uint32_t n = 0;
    vk_check(vkEnumeratePhysicalDevices(instance, &n, nullptr),
             "enumerate devices");
    std::vector<VkPhysicalDevice> devices(n);
    vk_check(vkEnumeratePhysicalDevices(instance, &n, devices.data()),
             "enumerate devices");
    for (const auto candidate : devices) {
      std::uint32_t extension_count = 0;
      vk_check(vkEnumerateDeviceExtensionProperties(candidate, nullptr,
                                                    &extension_count, nullptr),
               "enumerate device extensions");
      std::vector<VkExtensionProperties> extensions(extension_count);
      vk_check(vkEnumerateDeviceExtensionProperties(
                   candidate, nullptr, &extension_count, extensions.data()),
               "enumerate device extensions");
      if (std::none_of(extensions.begin(), extensions.end(), [](const auto& p) {
            return std::strcmp(p.extensionName,
                               VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
          }))
        continue;
      std::uint32_t queues = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queues, nullptr);
      std::vector<VkQueueFamilyProperties> properties(queues);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queues,
                                               properties.data());
      for (std::uint32_t i = 0; i < queues; ++i) {
        VkBool32 present = VK_FALSE;
        vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface,
                                                      &present),
                 "query present support");
        if (present && (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
          physical = candidate;
          family = i;
          break;
        }
      }
      if (physical) break;
    }
    if (!physical)
      throw std::runtime_error(
          "No Vulkan device with a combined graphics/present queue and "
          "VK_KHR_swapchain");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    std::cout << "Vulkan " << VK_VERSION_MAJOR(properties.apiVersion) << '.'
              << VK_VERSION_MINOR(properties.apiVersion) << '.'
              << VK_VERSION_PATCH(properties.apiVersion) << " | "
              << properties.deviceName << '\n';
    const float priority = 1;
    auto queue_info = info<VkDeviceQueueCreateInfo>(
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    auto create =
        info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    create.queueCreateInfoCount = 1;
    create.pQueueCreateInfos = &queue_info;
    create.enabledExtensionCount = 1;
    create.ppEnabledExtensionNames = &extension;
    vk_check(vkCreateDevice(physical, &create, nullptr, &device),
             "create device");
    vkGetDeviceQueue(device, family, 0, &queue);
  }
  void destroy_swapchain() {
    for (auto fb : framebuffers)
      if (fb) vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers.clear();
    if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
    if (render_pass) vkDestroyRenderPass(device, render_pass, nullptr);
    render_pass = VK_NULL_HANDLE;
    for (auto view : views)
      if (view) vkDestroyImageView(device, view, nullptr);
    views.clear();
    for (auto sem : presented)
      if (sem) vkDestroySemaphore(device, sem, nullptr);
    presented.clear();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
  }
  void recreate() {
    // Device idle is restricted to resize/teardown, never the frame loop.
    vk_check(vkDeviceWaitIdle(device), "wait before swapchain recreation");
    destroy_swapchain();
    VkSurfaceCapabilitiesKHR caps{};
    vk_check(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps),
        "query surface capabilities");
    extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
      extent.width = std::clamp(window.width, caps.minImageExtent.width,
                                caps.maxImageExtent.width);
      extent.height = std::clamp(window.height, caps.minImageExtent.height,
                                 caps.maxImageExtent.height);
    }
    if (!extent.width || !extent.height) return;
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
      throw std::runtime_error("Surface does not support color attachments");
    std::uint32_t count = 0;
    vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count,
                                                  nullptr),
             "query formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count,
                                                  formats.data()),
             "query formats");
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
      formats[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    const bool srgb = std::getenv("GLYPHSHIFT_FIXTURE_SRGB") != nullptr;
    const auto selected =
        std::find_if(formats.begin(), formats.end(), [&](const auto& f) {
          const bool compatible =
              srgb ? f.format == VK_FORMAT_B8G8R8A8_SRGB ||
                         f.format == VK_FORMAT_R8G8B8A8_SRGB
                   : f.format == VK_FORMAT_B8G8R8A8_UNORM ||
                         f.format == VK_FORMAT_R8G8B8A8_UNORM;
          return compatible &&
                 f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    if (selected == formats.end())
      throw std::runtime_error(
          "Fixture requires an SDR RGBA/BGRA UNORM surface");
    auto create = info<VkSwapchainCreateInfoKHR>(
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    create.surface = surface;
    create.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount)
      create.minImageCount = std::min(create.minImageCount, caps.maxImageCount);
    create.imageFormat = selected->format;
    create.imageColorSpace = selected->colorSpace;
    create.imageExtent = extent;
    create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.preTransform = caps.currentTransform;
    for (const auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                             VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                             VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                             VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
      if (caps.supportedCompositeAlpha & alpha) {
        create.compositeAlpha = alpha;
        break;
      }
    }
    create.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    create.clipped = VK_TRUE;
    vk_check(vkCreateSwapchainKHR(device, &create, nullptr, &swapchain),
             "create swapchain");
    vk_check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr),
             "get swapchain images");
    std::vector<VkImage> images(count);
    vk_check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()),
             "get swapchain images");
    make_pipeline(selected->format);
    views.resize(count);
    framebuffers.resize(count);
    presented.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      auto view =
          info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
      view.image = images[i];
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = selected->format;
      view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vk_check(vkCreateImageView(device, &view, nullptr, &views[i]),
               "create image view");
      auto fb = info<VkFramebufferCreateInfo>(
          VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
      fb.renderPass = render_pass;
      fb.attachmentCount = 1;
      fb.pAttachments = &views[i];
      fb.width = extent.width;
      fb.height = extent.height;
      fb.layers = 1;
      vk_check(vkCreateFramebuffer(device, &fb, nullptr, &framebuffers[i]),
               "create framebuffer");
      const auto semaphore =
          info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
      vk_check(vkCreateSemaphore(device, &semaphore, nullptr, &presented[i]),
               "create present semaphore");
    }
  }
  void make_pipeline(VkFormat format) {
    VkAttachmentDescription color{};
    color.format = format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference reference{0,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    auto pass =
        info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    pass.attachmentCount = 1;
    pass.pAttachments = &color;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 1;
    pass.pDependencies = &dependency;
    vk_check(vkCreateRenderPass(device, &pass, nullptr, &render_pass),
             "create render pass");
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (std::size_t i = 0; i < stages.size(); ++i) {
      stages[i] = info<VkPipelineShaderStageCreateInfo>(
          VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
      stages[i].stage =
          i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
      stages[i].module = shaders[i];
      stages[i].pName = "main";
    }
    const auto vertex = info<VkPipelineVertexInputStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    auto assembly = info<VkPipelineInputAssemblyStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto viewport = info<VkPipelineViewportStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    auto raster = info<VkPipelineRasterizationStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    auto samples = info<VkPipelineMultisampleStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    auto blend = info<VkPipelineColorBlendStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const std::array<VkDynamicState, 2> states{VK_DYNAMIC_STATE_VIEWPORT,
                                               VK_DYNAMIC_STATE_SCISSOR};
    auto dynamic = info<VkPipelineDynamicStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(states.size());
    dynamic.pDynamicStates = states.data();
    auto create = info<VkGraphicsPipelineCreateInfo>(
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    create.stageCount = static_cast<std::uint32_t>(stages.size());
    create.pStages = stages.data();
    create.pVertexInputState = &vertex;
    create.pInputAssemblyState = &assembly;
    create.pViewportState = &viewport;
    create.pRasterizationState = &raster;
    create.pMultisampleState = &samples;
    create.pColorBlendState = &blend;
    create.pDynamicState = &dynamic;
    create.layout = layout;
    create.renderPass = render_pass;
    vk_check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &create,
                                       nullptr, &pipeline),
             "create pipeline");
  }
  bool draw(Frame& frame, float time) {
    vk_check(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX),
             "wait for frame");
    std::uint32_t index = 0;
    const auto acquired = vkAcquireNextImageKHR(
        device, swapchain, UINT64_MAX, frame.acquired, VK_NULL_HANDLE, &index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
      recreate();
      return false;
    }
    if (acquired != VK_SUBOPTIMAL_KHR) vk_check(acquired, "acquire image");
    vk_check(vkResetCommandBuffer(frame.command, 0), "reset command buffer");
    auto begin = info<VkCommandBufferBeginInfo>(
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_check(vkBeginCommandBuffer(frame.command, &begin),
             "begin command buffer");
    auto pass =
        info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    pass.renderPass = render_pass;
    pass.framebuffer = framebuffers[index];
    pass.renderArea.extent = extent;
    vkCmdBeginRenderPass(frame.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(frame.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{0,
                              0,
                              static_cast<float>(extent.width),
                              static_cast<float>(extent.height),
                              0,
                              1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(frame.command, 0, 1, &viewport);
    vkCmdSetScissor(frame.command, 0, 1, &scissor);
    const std::array<float, 3> parameters{static_cast<float>(extent.width),
                                          static_cast<float>(extent.height),
                                          time};
    vkCmdPushConstants(frame.command, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(parameters), parameters.data());
    vkCmdDraw(frame.command, 3, 1, 0, 0);
    vkCmdEndRenderPass(frame.command);
    vk_check(vkEndCommandBuffer(frame.command), "end command buffer");
    const VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    auto submit = info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.acquired;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &presented[index];
    vk_check(vkResetFences(device, 1, &frame.fence), "reset frame fence");
    vk_check(vkQueueSubmit(queue, 1, &submit, frame.fence), "submit frame");
    auto present = info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &presented[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &index;
    const auto result = vkQueuePresentKHR(queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        acquired == VK_SUBOPTIMAL_KHR) {
      if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
        vk_check(result, "present");
      recreate();
    } else
      vk_check(result, "present");
    return true;
  }
  void run(const glyphshift::FixtureOptions& opts) {
    std::uint64_t frame = 0;
    const auto start = std::chrono::steady_clock::now();
    while ((!opts.frames || frame < opts.frames) && window.poll()) {
      if (!window.width || !window.height) {
        fixture::pace(std::chrono::steady_clock::now(), 1);
        continue;
      }
      if (window.resized || !swapchain) recreate();
      if (!swapchain) {
        fixture::pace(std::chrono::steady_clock::now(), 1);
        continue;
      }
      if (draw(frames[frame % frames.size()],
               glyphshift::fixture_time(frame, opts)))
        fixture::pace(start, ++frame);
    }
    vk_check(vkDeviceWaitIdle(device), "wait for shutdown");
    std::cout << "Vulkan fixture: clean shutdown after " << frame
              << " frames\n";
  }
};
}  // namespace
int main(int argc, char** argv) {
  try {
    const auto opts = fixture::options(argc, argv, true);
    if (opts.help) return 0;
    {
      Renderer renderer;
      renderer.init(opts.validation);
      renderer.run(opts);
    }
    if (validation_errors) {
      std::cerr << validation_errors << " Vulkan validation errors\n";
      return 1;
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Vulkan fixture: " << error.what() << '\n';
    return 1;
  }
}
