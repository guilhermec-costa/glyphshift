#include "renderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <glyphshift/core/glyph.hpp>
#include <glyphshift/editor/editor.hpp>
#include <stdexcept>
#include <string>

#include "dispatch.hpp"
#include "editor_frag_spv.hpp"
#include "editor_vert_spv.hpp"
#include "glyph_frag_spv.hpp"
#include "glyph_vert_spv.hpp"

namespace glyphshift::vulkan {
namespace {
template <class T>
T info(VkStructureType type) {
  T result{};
  result.sType = type;
  return result;
}
void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(std::string(operation) + ": VkResult " +
                             std::to_string(result));
}
VkFormat capture_format(VkFormat format) {
  switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
      return VK_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
      return VK_FORMAT_R8G8B8A8_UNORM;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}
void warning(DeviceDispatch& device, const char* text) {
  if (!device.render_warning.test_and_set(std::memory_order_relaxed))
    diagnostic(text);
}
struct Texture {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};
struct Frame {
  VkBuffer ui_buffer = VK_NULL_HANDLE;
  VkDeviceMemory ui_memory = VK_NULL_HANDLE;
  void* ui_mapped = nullptr;
  VkDeviceSize ui_capacity = 0;
  Texture capture;
  VkImage image = VK_NULL_HANDLE;
  VkImageView output_view = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkCommandBuffer command = VK_NULL_HANDLE;
  VkSemaphore completed = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkDescriptorSet descriptors = VK_NULL_HANDLE;
};
struct Parameters {
  float width, height, cell_width, cell_height;
  uint32_t source_color, ramp_count, output_srgb;
};
static_assert(sizeof(Parameters) == 28);
}  // namespace

void initialize_renderer_api(DeviceDispatch& state) {
  auto& api = state.render_api;
  api.available = true;
#define LOAD(name)                               \
  api.name = reinterpret_cast<PFN_vk##name>(     \
      state.get_proc(state.handle, "vk" #name)); \
  api.available &= api.name != nullptr;
  GLYPHSHIFT_VK_RENDER_FUNCTIONS(LOAD)
#undef LOAD
  api.available &= state.set_loader_data && state.instance->memory_properties &&
                   state.instance->queue_properties &&
                   state.instance->surface_capabilities &&
                   state.instance->format_properties && state.get_images;
}
bool negotiate_capture(DeviceDispatch& state,
                       const VkSwapchainCreateInfoKHR& input,
                       VkSwapchainCreateInfoKHR& output) noexcept {
  if (!state.render_api.available || !capture_format(input.imageFormat) ||
      input.flags || input.imageArrayLayers != 1 ||
      input.imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    return false;
  VkSurfaceCapabilitiesKHR capabilities{};
  if (state.instance->surface_capabilities(state.physical, input.surface,
                                           &capabilities) != VK_SUCCESS)
    return false;
  constexpr auto usage =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if ((capabilities.supportedUsageFlags & usage) != usage) return false;
  output = input;
  output.imageUsage |= usage;
  return true;
}

class GlyphRenderer {
 public:
  GlyphRenderer(DeviceDispatch& device, VkSwapchainKHR swapchain,
                const SwapchainState& state, VkQueue queue,
                QueueState queue_state)
      : device_(device),
        api_(device.render_api),
        swapchain_(swapchain),
        extent_(state.extent),
        format_(state.format),
        queue_(queue),
        queue_state_(queue_state) {}
  ~GlyphRenderer() {
    const auto device = device_.handle;
    // This wait is teardown/resize only. Ordinary frames wait only their own
    // submission fence; presentation semaphores are indexed by swapchain image.
    if (submitted_) api_.QueueWaitIdle(queue_);
    if (pool_) api_.DestroyCommandPool(device, pool_, nullptr);
    if (descriptor_pool_)
      api_.DestroyDescriptorPool(device, descriptor_pool_, nullptr);
    for (auto& frame : frames_) {
      if (frame.framebuffer)
        api_.DestroyFramebuffer(device, frame.framebuffer, nullptr);
      if (frame.output_view)
        api_.DestroyImageView(device, frame.output_view, nullptr);
      if (frame.ui_mapped) api_.UnmapMemory(device, frame.ui_memory);
      if (frame.ui_buffer) api_.DestroyBuffer(device, frame.ui_buffer, nullptr);
      if (frame.ui_memory) api_.FreeMemory(device, frame.ui_memory, nullptr);
      destroy(frame.capture);
      if (frame.completed)
        api_.DestroySemaphore(device, frame.completed, nullptr);
      if (frame.fence) api_.DestroyFence(device, frame.fence, nullptr);
    }
    if (ui_pipeline_) api_.DestroyPipeline(device, ui_pipeline_, nullptr);
    if (pipeline_) api_.DestroyPipeline(device, pipeline_, nullptr);
    if (render_pass_) api_.DestroyRenderPass(device, render_pass_, nullptr);
    for (const auto module : modules_)
      if (module) api_.DestroyShaderModule(device, module, nullptr);
    if (layout_) api_.DestroyPipelineLayout(device, layout_, nullptr);
    if (descriptor_layout_)
      api_.DestroyDescriptorSetLayout(device, descriptor_layout_, nullptr);
    if (sampler_) api_.DestroySampler(device, sampler_, nullptr);
    destroy(font_);
    destroy(atlas_);
    destroy(ramp_);
    if (staging_) api_.DestroyBuffer(device, staging_, nullptr);
    if (staging_memory_) api_.FreeMemory(device, staging_memory_, nullptr);
  }
  void initialize() {
    uint32_t family_count = 0;
    device_.instance->queue_properties(device_.physical, &family_count,
                                       nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    device_.instance->queue_properties(device_.physical, &family_count,
                                       families.data());
    if (queue_state_.flags || queue_state_.family >= family_count ||
        !(families[queue_state_.family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
      throw std::runtime_error(
          "present queue is not an unprotected graphics queue");
    if (!extent_.width || !extent_.height || !capture_format(format_))
      throw std::runtime_error("unsupported swapchain extent/format");
    device_.instance->memory_properties(device_.physical, &memory_);
    for (const auto format :
         {capture_format(format_), VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UINT}) {
      VkFormatProperties properties{};
      device_.instance->format_properties(device_.physical, format,
                                          &properties);
      if (!(properties.optimalTilingFeatures &
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        throw std::runtime_error(
            "capture/atlas texture format cannot be sampled");
    }
    uint32_t count = 0;
    check(device_.get_images(device_.handle, swapchain_, &count, nullptr),
          "query swapchain image count");
    if (!count || count > 256)
      throw std::runtime_error("unsupported swapchain image count");
    std::vector<VkImage> images(count);
    check(device_.get_images(device_.handle, swapchain_, &count, images.data()),
          "query swapchain images");
    images.resize(count);
    frames_.resize(count);
    auto pool = info<VkCommandPoolCreateInfo>(
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queue_state_.family;
    check(api_.CreateCommandPool(device_.handle, &pool, nullptr, &pool_),
          "create glyph command pool");
    texture(atlas_, VK_FORMAT_R8_UNORM, {atlas_width, atlas_height});
    texture(ramp_, VK_FORMAT_R8_UINT,
            {static_cast<uint32_t>(default_ramp.size()), 1});
    const auto font = editor::font();
    font_extent_ = {static_cast<uint32_t>(font.width),
                    static_cast<uint32_t>(font.height)};
    texture(font_, VK_FORMAT_R8_UNORM, font_extent_);
    staging();
    descriptors();
    pipeline();
    auto semaphore =
        info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    auto fence = info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    auto allocation = info<VkCommandBufferAllocateInfo>(
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocation.commandPool = pool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    for (uint32_t i = 0; i < count; ++i) {
      auto& frame = frames_[i];
      frame.image = images[i];
      texture(frame.capture, capture_format(format_), extent_);
      frame.output_view = image_view(frame.image, format_);
      auto framebuffer = info<VkFramebufferCreateInfo>(
          VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
      framebuffer.renderPass = render_pass_;
      framebuffer.attachmentCount = 1;
      framebuffer.pAttachments = &frame.output_view;
      framebuffer.width = extent_.width;
      framebuffer.height = extent_.height;
      framebuffer.layers = 1;
      check(api_.CreateFramebuffer(device_.handle, &framebuffer, nullptr,
                                   &frame.framebuffer),
            "create glyph framebuffer");
      check(api_.CreateSemaphore(device_.handle, &semaphore, nullptr,
                                 &frame.completed),
            "create glyph present semaphore");
      check(api_.CreateFence(device_.handle, &fence, nullptr, &frame.fence),
            "create glyph fence");
      check(api_.AllocateCommandBuffers(device_.handle, &allocation,
                                        &frame.command),
            "allocate glyph command buffer");
      ensure_ui_buffer(frame, 256 * 1024);
      check(device_.set_loader_data(device_.handle, frame.command),
            "initialize layer command-buffer dispatch");
      auto descriptor = info<VkDescriptorSetAllocateInfo>(
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
      descriptor.descriptorPool = descriptor_pool_;
      descriptor.descriptorSetCount = 1;
      descriptor.pSetLayouts = &descriptor_layout_;
      check(api_.AllocateDescriptorSets(device_.handle, &descriptor,
                                        &frame.descriptors),
            "allocate glyph descriptors");
      const std::array<VkDescriptorImageInfo, 4> bindings{
          {{sampler_, frame.capture.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
           {sampler_, atlas_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
           {sampler_, ramp_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
           {sampler_, font_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
      std::array<VkWriteDescriptorSet, 4> writes{};
      for (uint32_t binding = 0; binding < writes.size(); ++binding) {
        writes[binding] =
            info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[binding].dstSet = frame.descriptors;
        writes[binding].dstBinding = binding;
        writes[binding].descriptorCount = 1;
        writes[binding].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[binding].pImageInfo = &bindings[binding];
      }
      api_.UpdateDescriptorSets(device_.handle,
                                static_cast<uint32_t>(writes.size()),
                                writes.data(), 0, nullptr);
    }
    diagnostic("glyphshift: Vulkan glyph renderer initialized\n");
  }
  VkSemaphore draw(VkQueue queue, uint32_t index,
                   const VkPresentInfoKHR& present, const Settings& settings,
                   const ImDrawData* ui) {
    if (queue != queue_ || index >= frames_.size()) return VK_NULL_HANDLE;
    const std::lock_guard lock(mutex_);
    auto& frame = frames_[index];
    check(api_.WaitForFences(device_.handle, 1, &frame.fence, VK_TRUE,
                             UINT64_MAX),
          "wait for glyph frame");
    if (ui) upload_ui(frame, *ui);
    check(api_.ResetCommandBuffer(frame.command, 0),
          "reset glyph command buffer");
    auto begin = info<VkCommandBufferBeginInfo>(
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(api_.BeginCommandBuffer(frame.command, &begin),
          "begin glyph command buffer");
    if (!uploaded_) {
      upload(frame.command, atlas_, 0, {atlas_width, atlas_height});
      upload(frame.command, ramp_, default_atlas.size(),
             {static_cast<uint32_t>(default_ramp.size()), 1});
      upload(frame.command, font_, font_offset_, font_extent_);
    }
    barrier(frame.command, frame.image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    barrier(frame.command, frame.capture.image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageCopy copy{};
    copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.dstSubresource = copy.srcSubresource;
    copy.extent = {extent_.width, extent_.height, 1};
    api_.CmdCopyImage(frame.command, frame.image,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame.capture.image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(frame.command, frame.capture.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    barrier(frame.command, frame.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    auto pass =
        info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    pass.renderPass = render_pass_;
    pass.framebuffer = frame.framebuffer;
    pass.renderArea.extent = extent_;
    api_.CmdBeginRenderPass(frame.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    api_.CmdBindPipeline(frame.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         pipeline_);
    const VkViewport viewport{0,
                              0,
                              static_cast<float>(extent_.width),
                              static_cast<float>(extent_.height),
                              0,
                              1};
    const VkRect2D scissor{{0, 0}, extent_};
    api_.CmdSetViewport(frame.command, 0, 1, &viewport);
    api_.CmdSetScissor(frame.command, 0, 1, &scissor);
    api_.CmdBindDescriptorSets(frame.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               layout_, 0, 1, &frame.descriptors, 0, nullptr);
    const Parameters parameters{static_cast<float>(extent_.width),
                                static_cast<float>(extent_.height),
                                static_cast<float>(settings.cell_width),
                                static_cast<float>(settings.cell_height),
                                settings.source_color ? 1U : 0U,
                                static_cast<uint32_t>(default_ramp.size()),
                                (format_ == VK_FORMAT_B8G8R8A8_SRGB ||
                                 format_ == VK_FORMAT_R8G8B8A8_SRGB)
                                    ? 1U
                                    : 0U};
    api_.CmdPushConstants(
        frame.command, layout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(parameters), &parameters);
    if (settings.enabled) api_.CmdDraw(frame.command, 3, 1, 0, 0);
    if (ui) draw_ui(frame, *ui);
    api_.CmdEndRenderPass(frame.command);
    check(api_.EndCommandBuffer(frame.command), "end glyph command buffer");
    // Storage for wait masks is allocated once with the renderer. Refuse an
    // unexpectedly huge wait list instead of allocating in the frame loop.
    if (present.waitSemaphoreCount > wait_masks_.size()) return VK_NULL_HANDLE;
    auto submit = info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.waitSemaphoreCount = present.waitSemaphoreCount;
    submit.pWaitSemaphores = present.pWaitSemaphores;
    submit.pWaitDstStageMask = wait_masks_.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &frame.completed;
    check(api_.ResetFences(device_.handle, 1, &frame.fence),
          "reset glyph fence");
    check(api_.QueueSubmit(queue_, 1, &submit, frame.fence),
          "submit glyph work");
    uploaded_ = submitted_ = true;
    return frame.completed;
  }

 private:
  DeviceDispatch& device_;
  const RenderApi& api_;
  VkSwapchainKHR swapchain_;
  VkExtent2D extent_;
  VkFormat format_;
  VkQueue queue_;
  QueueState queue_state_;
  VkPhysicalDeviceMemoryProperties memory_{};
  std::mutex mutex_;
  bool uploaded_ = false, submitted_ = false;
  std::vector<Frame> frames_;
  Texture atlas_, ramp_, font_;
  VkExtent2D font_extent_{};
  static constexpr VkDeviceSize font_offset_ =
      (default_atlas.size() + default_ramp.size() + 3) & ~VkDeviceSize{3};
  VkBuffer staging_ = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  std::array<VkShaderModule, 4> modules_{};
  const std::array<VkPipelineStageFlags, 64> wait_masks_ = [] {
    std::array<VkPipelineStageFlags, 64> values{};
    values.fill(VK_PIPELINE_STAGE_TRANSFER_BIT);
    return values;
  }();
  uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < memory_.memoryTypeCount; ++i)
      if ((bits & (1U << i)) &&
          (memory_.memoryTypes[i].propertyFlags & flags) == flags)
        return i;
    throw std::runtime_error("no compatible glyph memory type");
  }
  void allocate(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags,
                VkDeviceMemory& output) {
    auto allocation =
        info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex =
        memory_type(requirements.memoryTypeBits, flags);
    check(api_.AllocateMemory(device_.handle, &allocation, nullptr, &output),
          "allocate glyph memory");
  }
  VkImageView image_view(VkImage image, VkFormat format) {
    auto view =
        info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view.image = image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView result{};
    check(api_.CreateImageView(device_.handle, &view, nullptr, &result),
          "create glyph image view");
    return result;
  }
  void texture(Texture& target, VkFormat format, VkExtent2D extent) {
    auto image = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {extent.width, extent.height, 1};
    image.mipLevels = image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(api_.CreateImage(device_.handle, &image, nullptr, &target.image),
          "create glyph texture");
    VkMemoryRequirements requirements{};
    api_.GetImageMemoryRequirements(device_.handle, target.image,
                                    &requirements);
    allocate(requirements, 0, target.memory);
    check(api_.BindImageMemory(device_.handle, target.image, target.memory, 0),
          "bind glyph texture memory");
    target.view = image_view(target.image, format);
  }
  void destroy(Texture& texture) {
    if (texture.view)
      api_.DestroyImageView(device_.handle, texture.view, nullptr);
    if (texture.image)
      api_.DestroyImage(device_.handle, texture.image, nullptr);
    if (texture.memory)
      api_.FreeMemory(device_.handle, texture.memory, nullptr);
  }
  void staging() {
    const auto font = editor::font();
    const auto bytes =
        font_offset_ + static_cast<VkDeviceSize>(font.width) *
                           static_cast<VkDeviceSize>(font.height);
    auto buffer =
        info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    buffer.size = bytes;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(api_.CreateBuffer(device_.handle, &buffer, nullptr, &staging_),
          "create atlas staging buffer");
    VkMemoryRequirements requirements{};
    api_.GetBufferMemoryRequirements(device_.handle, staging_, &requirements);
    allocate(requirements,
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
             staging_memory_);
    check(api_.BindBufferMemory(device_.handle, staging_, staging_memory_, 0),
          "bind atlas staging memory");
    void* mapped = nullptr;
    check(api_.MapMemory(device_.handle, staging_memory_, 0, bytes, 0, &mapped),
          "map atlas upload");
    std::memcpy(mapped, default_atlas.data(), default_atlas.size());
    std::memcpy(static_cast<std::uint8_t*>(mapped) + default_atlas.size(),
                default_ramp.data(), default_ramp.size());
    std::memcpy(static_cast<std::uint8_t*>(mapped) + font_offset_, font.pixels,
                static_cast<std::size_t>(font.width) *
                    static_cast<std::size_t>(font.height));
    api_.UnmapMemory(device_.handle, staging_memory_);
  }
  void descriptors() {
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i)
      bindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    auto layout = info<VkDescriptorSetLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = static_cast<uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    check(api_.CreateDescriptorSetLayout(device_.handle, &layout, nullptr,
                                         &descriptor_layout_),
          "create glyph descriptor layout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              static_cast<uint32_t>(frames_.size() * 4)};
    auto pool = info<VkDescriptorPoolCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.maxSets = static_cast<uint32_t>(frames_.size());
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(api_.CreateDescriptorPool(device_.handle, &pool, nullptr,
                                    &descriptor_pool_),
          "create glyph descriptor pool");
    auto sampler =
        info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(api_.CreateSampler(device_.handle, &sampler, nullptr, &sampler_),
          "create glyph sampler");
  }
  void pipeline() {
    const std::array<std::span<const uint32_t>, 4> code{
        shaders::glyph_vert_spv, shaders::glyph_frag_spv,
        shaders::editor_vert_spv, shaders::editor_frag_spv};
    for (unsigned i = 0; i < code.size(); ++i) {
      auto module = info<VkShaderModuleCreateInfo>(
          VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
      module.codeSize = code[i].size_bytes();
      module.pCode = code[i].data();
      check(api_.CreateShaderModule(device_.handle, &module, nullptr,
                                    &modules_[i]),
            "create glyph SPIR-V module");
    }
    const VkPushConstantRange range{
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(Parameters)};
    auto layout = info<VkPipelineLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &descriptor_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    check(api_.CreatePipelineLayout(device_.handle, &layout, nullptr, &layout_),
          "create glyph pipeline layout");
    VkAttachmentDescription color{};
    color.format = format_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    const VkAttachmentReference reference{
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    auto pass =
        info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    pass.attachmentCount = 1;
    pass.pAttachments = &color;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    check(api_.CreateRenderPass(device_.handle, &pass, nullptr, &render_pass_),
          "create glyph render pass");
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (unsigned i = 0; i < stages.size(); ++i) {
      stages[i] = info<VkPipelineShaderStageCreateInfo>(
          VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
      stages[i].stage =
          i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
      stages[i].module = modules_[i];
      stages[i].pName = "main";
    }
    auto vertex = info<VkPipelineVertexInputStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    auto assembly = info<VkPipelineInputAssemblyStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto viewport = info<VkPipelineViewportStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = viewport.scissorCount = 1;
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
    dynamic.dynamicStateCount = static_cast<uint32_t>(states.size());
    dynamic.pDynamicStates = states.data();
    auto create = info<VkGraphicsPipelineCreateInfo>(
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    create.stageCount = static_cast<uint32_t>(stages.size());
    create.pStages = stages.data();
    create.pVertexInputState = &vertex;
    create.pInputAssemblyState = &assembly;
    create.pViewportState = &viewport;
    create.pRasterizationState = &raster;
    create.pMultisampleState = &samples;
    create.pColorBlendState = &blend;
    create.pDynamicState = &dynamic;
    create.layout = layout_;
    create.renderPass = render_pass_;
    check(api_.CreateGraphicsPipelines(device_.handle, VK_NULL_HANDLE, 1,
                                       &create, nullptr, &pipeline_),
          "create glyph graphics pipeline");
    stages[0].module = modules_[2];
    stages[1].module = modules_[3];
    const VkVertexInputBindingDescription binding{0, sizeof(ImDrawVert),
                                                  VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 3> attributes{
        {{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)},
         {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)},
         {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)}}};
    vertex.vertexBindingDescriptionCount = 1;
    vertex.pVertexBindingDescriptions = &binding;
    vertex.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributes.size());
    vertex.pVertexAttributeDescriptions = attributes.data();
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    check(api_.CreateGraphicsPipelines(device_.handle, VK_NULL_HANDLE, 1,
                                       &create, nullptr, &ui_pipeline_),
          "create editor graphics pipeline");
  }
  void ensure_ui_buffer(Frame& frame, VkDeviceSize required) {
    if (required <= frame.ui_capacity) return;
    if (required > 16 * 1024 * 1024)
      throw std::runtime_error("editor geometry limit exceeded");
    if (frame.ui_mapped) {
      api_.UnmapMemory(device_.handle, frame.ui_memory);
      frame.ui_mapped = nullptr;
    }
    if (frame.ui_buffer) {
      api_.DestroyBuffer(device_.handle, frame.ui_buffer, nullptr);
      frame.ui_buffer = VK_NULL_HANDLE;
    }
    if (frame.ui_memory) {
      api_.FreeMemory(device_.handle, frame.ui_memory, nullptr);
      frame.ui_memory = VK_NULL_HANDLE;
    }
    frame.ui_capacity = std::max(required, frame.ui_capacity * 2);
    auto buffer =
        info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    buffer.size = frame.ui_capacity;
    buffer.usage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(api_.CreateBuffer(device_.handle, &buffer, nullptr, &frame.ui_buffer),
          "create editor geometry buffer");
    VkMemoryRequirements requirements{};
    api_.GetBufferMemoryRequirements(device_.handle, frame.ui_buffer,
                                     &requirements);
    allocate(requirements,
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
             frame.ui_memory);
    check(api_.BindBufferMemory(device_.handle, frame.ui_buffer,
                                frame.ui_memory, 0),
          "bind editor geometry memory");
    check(api_.MapMemory(device_.handle, frame.ui_memory, 0, frame.ui_capacity,
                         0, &frame.ui_mapped),
          "map editor geometry");
  }
  void upload_ui(Frame& frame, const ImDrawData& data) {
    const auto vertices =
        static_cast<VkDeviceSize>(data.TotalVtxCount) * sizeof(ImDrawVert);
    const auto indices =
        static_cast<VkDeviceSize>(data.TotalIdxCount) * sizeof(ImDrawIdx);
    ensure_ui_buffer(frame, vertices + indices);
    auto* v = static_cast<unsigned char*>(frame.ui_mapped);
    auto* i = v + vertices;
    for (const auto* list : data.CmdLists) {
      const auto vs =
          static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
      const auto is =
          static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
      std::memcpy(v, list->VtxBuffer.Data, vs);
      std::memcpy(i, list->IdxBuffer.Data, is);
      v += vs;
      i += is;
    }
  }
  void draw_ui(Frame& frame, const ImDrawData& data) {
    if (!data.TotalVtxCount || !data.TotalIdxCount) return;
    const VkDeviceSize offset = 0;
    api_.CmdBindPipeline(frame.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         ui_pipeline_);
    api_.CmdBindVertexBuffers(frame.command, 0, 1, &frame.ui_buffer, &offset);
    api_.CmdBindIndexBuffer(
        frame.command, frame.ui_buffer,
        static_cast<VkDeviceSize>(data.TotalVtxCount) * sizeof(ImDrawVert),
        sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    struct Projection {
      float scale[2], translate[2];
      uint32_t srgb;
    } projection{{2.0F / static_cast<float>(extent_.width),
                  2.0F / static_cast<float>(extent_.height)},
                 {-1, -1},
                 (format_ == VK_FORMAT_B8G8R8A8_SRGB ||
                  format_ == VK_FORMAT_R8G8B8A8_SRGB)
                     ? 1U
                     : 0U};
    api_.CmdPushConstants(
        frame.command, layout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(projection), &projection);
    uint32_t first_index = 0;
    int32_t vertex_offset = 0;
    for (const auto* list : data.CmdLists) {
      for (const auto& cmd : list->CmdBuffer) {
        if (cmd.UserCallback) continue;
        const int x = std::max(0, static_cast<int>(cmd.ClipRect.x));
        const int y = std::max(0, static_cast<int>(cmd.ClipRect.y));
        const int right = std::min(static_cast<int>(extent_.width),
                                   static_cast<int>(cmd.ClipRect.z));
        const int bottom = std::min(static_cast<int>(extent_.height),
                                    static_cast<int>(cmd.ClipRect.w));
        if (right <= x || bottom <= y) continue;
        const VkRect2D clip{{x, y},
                            {static_cast<uint32_t>(right - x),
                             static_cast<uint32_t>(bottom - y)}};
        api_.CmdSetScissor(frame.command, 0, 1, &clip);
        api_.CmdDrawIndexed(
            frame.command, cmd.ElemCount, 1, first_index + cmd.IdxOffset,
            vertex_offset + static_cast<int32_t>(cmd.VtxOffset), 0);
      }
      first_index += static_cast<uint32_t>(list->IdxBuffer.Size);
      vertex_offset += list->VtxBuffer.Size;
    }
  }
  void barrier(VkCommandBuffer command, VkImage image, VkImageLayout old_layout,
               VkImageLayout new_layout, VkAccessFlags source,
               VkAccessFlags destination, VkPipelineStageFlags source_stage,
               VkPipelineStageFlags destination_stage) {
    auto barrier =
        info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.srcAccessMask = source;
    barrier.dstAccessMask = destination;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    api_.CmdPipelineBarrier(command, source_stage, destination_stage, 0, 0,
                            nullptr, 0, nullptr, 1, &barrier);
  }
  void upload(VkCommandBuffer command, const Texture& texture,
              VkDeviceSize offset, VkExtent2D extent) {
    barrier(command, texture.image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.bufferOffset = offset;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {extent.width, extent.height, 1};
    api_.CmdCopyBufferToImage(command, staging_, texture.image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(command, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  }
};

VkResult render_present(DeviceDispatch& device, VkQueue queue,
                        const VkPresentInfoKHR* info) {
  if (!device.render_api.available) return device.present(queue, info);
  if (info->swapchainCount != 1 || info->pNext ||
      info->waitSemaphoreCount > 64) {
    warning(device,
            "glyphshift: unsupported Vulkan present batch/pNext; forwarding "
            "unchanged\n");
    return device.present(queue, info);
  }
  std::shared_ptr<SwapchainState> state;
  QueueState queue_state;
  {
    const std::lock_guard lock(device.mutex);
    const auto found = device.swapchains.find(info->pSwapchains[0]);
    const auto known_queue = device.queues.find(queue);
    if (found != device.swapchains.end() &&
        known_queue != device.queues.end()) {
      state = found->second;
      queue_state = known_queue->second;
    }
  }
  if (!state || !state->capture || !capture_format(state->format)) {
    warning(device,
            "glyphshift: unsupported Vulkan capture usage/format; forwarding "
            "unchanged\n");
    return device.present(queue, info);
  }
  const bool ownership =
      state->sharing == VK_SHARING_MODE_CONCURRENT
          ? std::find(state->families.begin(),
                      state->families.begin() + state->family_count,
                      queue_state.family) !=
                state->families.begin() + state->family_count
          : device.single_family && device.only_family == queue_state.family;
  if (!ownership || queue_state.flags) {
    warning(device,
            "glyphshift: unsupported Vulkan queue ownership/protection; "
            "forwarding unchanged\n");
    return device.present(queue, info);
  }
  const auto start = std::chrono::steady_clock::now();
  editor::Frame editor_frame(state->display, state->window, state->extent.width,
                             state->extent.height, "Vulkan",
                             device.version.data());
  if (!editor_frame.settings.enabled && !editor_frame.draw_data) {
    editor_frame.complete(0);
    return device.present(queue, info);
  }
  std::shared_ptr<GlyphRenderer> renderer;
  bool initialize = false, blocked = false;
  {
    const std::lock_guard lock(state->mutex);
    blocked = state->render_failed || state->initializing;
    renderer = state->renderer;
    if (!blocked && !renderer) {
      state->initializing = initialize = true;
    }
  }
  if (blocked) {
    editor_frame.complete(0);
    editor::deactivate(state->display, state->window);
    return device.present(queue, info);
  }
  try {
    if (initialize) {
      renderer = std::make_shared<GlyphRenderer>(device, info->pSwapchains[0],
                                                 *state, queue, queue_state);
      renderer->initialize();
      const std::lock_guard lock(state->mutex);
      state->renderer = renderer;
      state->initializing = false;
    }
    const auto completed =
        renderer->draw(queue, info->pImageIndices[0], *info,
                       editor_frame.settings, editor_frame.draw_data);
    editor_frame.complete(std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - start)
                              .count());
    if (!completed) {
      warning(device,
              "glyphshift: unsupported Vulkan queue change/image index; "
              "forwarding unchanged\n");
      return device.present(queue, info);
    }
    VkPresentInfoKHR presentation = *info;
    presentation.waitSemaphoreCount = 1;
    presentation.pWaitSemaphores = &completed;
    return device.present(queue, &presentation);
  } catch (const std::exception& error) {
    {
      const std::lock_guard lock(state->mutex);
      state->initializing = false;
      state->render_failed = true;
      state->renderer.reset();
    }
    diagnostic("glyphshift: Vulkan glyph renderer disabled for swapchain: ");
    diagnostic(error.what());
    editor::report(error.what());
    diagnostic("\n");
    editor_frame.complete(0);
    editor::deactivate(state->display, state->window);
    return device.present(queue, info);
  }
}
void release_renderers(DeviceDispatch& state) {
  decltype(state.swapchains) retired;
  {
    const std::lock_guard lock(state.mutex);
    retired.swap(state.swapchains);
  }
  retired.clear();
}
}  // namespace glyphshift::vulkan
