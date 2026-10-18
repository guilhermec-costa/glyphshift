#include "renderer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <glyphshift/editor/editor.hpp>
#include <memory>
#include <stdexcept>
#include <string>

#include "../../display/x11/editor_input.hpp"
#include "dispatch.hpp"
#include "pipeline.hpp"
#include "present_chain.hpp"

// The loader layer's use of the glyph pipeline: one pipeline target per
// swapchain image, fed by a GPU copy of the image the application presents,
// submitted on its presentation queue after its own semaphores.
namespace glyphshift::vulkan {
namespace {
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

struct Frame {
    Texture capture;
    VkImage image = VK_NULL_HANDLE;
    VkImageView output_view = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore completed = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
};
}  // namespace

void initialize_renderer_api(DeviceDispatch& state) {
    state.render_api = load_render_api(state.get_proc, state.handle);
    state.render_api.available &= state.set_loader_data && state.instance->memory_properties &&
                                  state.instance->queue_properties &&
                                  state.instance->surface_capabilities &&
                                  state.instance->format_properties && state.get_images;
}

bool negotiate_capture(DeviceDispatch& state,
    const VkSwapchainCreateInfoKHR& input,
    VkSwapchainCreateInfoKHR& output) noexcept {
    if (!state.render_api.available || !capture_format(input.imageFormat) ||
        !supported_swapchain_flags(input.flags) || input.imageArrayLayers != 1 ||
        input.imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        return false;
    VkSurfaceCapabilitiesKHR capabilities{};
    if (state.instance->surface_capabilities(state.physical, input.surface, &capabilities) !=
        VK_SUCCESS)
        return false;
    constexpr auto usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if ((capabilities.supportedUsageFlags & usage) != usage)
        return false;
    output = input;
    output.imageUsage |= usage;
    return true;
}

class GlyphRenderer {
public:
    GlyphRenderer(DeviceDispatch& device,
        VkSwapchainKHR swapchain,
        const SwapchainState& state,
        VkQueue queue,
        QueueState queue_state,
        const CharacterSelection& characters)
        : device_(device),
          api_(device.render_api),
          resources_(Device{device.handle,
              device.physical,
              &device.render_api,
              device.instance->memory_properties,
              device.instance->format_properties}),
          swapchain_(swapchain),
          extent_(state.extent),
          format_(state.format),
          queue_(queue),
          queue_state_(queue_state),
          selection_(characters) {}

    bool matches(const CharacterSelection& value) const {
        return selection_ == value;
    }

    void forget_history() {
        if (pipeline_)
            pipeline_->forget_history();
    }

    ~GlyphRenderer() {
        const auto device = device_.handle;
        // This wait is teardown/resize only. Ordinary frames wait only their own
        // submission fence; presentation semaphores are indexed by swapchain image.
        if (submitted_)
            api_.QueueWaitIdle(queue_);
        // The pipeline's framebuffers use the output views below.
        pipeline_.reset();
        if (pool_)
            api_.DestroyCommandPool(device, pool_, nullptr);
        for (auto& frame : frames_) {
            if (frame.output_view)
                api_.DestroyImageView(device, frame.output_view, nullptr);
            resources_.destroy(frame.capture);
            if (frame.completed)
                api_.DestroySemaphore(device, frame.completed, nullptr);
            if (frame.fence)
                api_.DestroyFence(device, frame.fence, nullptr);
        }
    }

    void initialize() {
        uint32_t family_count = 0;
        device_.instance->queue_properties(device_.physical, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        device_.instance->queue_properties(device_.physical, &family_count, families.data());
        if (queue_state_.flags || queue_state_.family >= family_count ||
            !(families[queue_state_.family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            throw std::runtime_error("present queue is not an unprotected graphics queue");
        if (!extent_.width || !extent_.height || !capture_format(format_))
            throw std::runtime_error("unsupported swapchain extent/format");
        if (!(resources_.features(capture_format(format_)) & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            throw std::runtime_error("capture texture format cannot be sampled");
        uint32_t count = 0;
        check(device_.get_images(device_.handle, swapchain_, &count, nullptr),
            "query swapchain image count");
        if (!count || count > 256)
            throw std::runtime_error("unsupported swapchain image count");
        std::vector<VkImage> images(count);
        check(device_.get_images(device_.handle, swapchain_, &count, images.data()),
            "query swapchain images");
        images.resize(count);
        pipeline_ = std::make_unique<Pipeline>(resources_,
            format_,
            extent_,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            count,
            selection_);
        if (const char* atlas = pipeline_->atlas_diagnostic(); atlas[0]) {
            diagnostic(atlas);
            diagnostic("\n");
        }
        frames_.resize(count);
        auto pool = info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = queue_state_.family;
        check(api_.CreateCommandPool(device_.handle, &pool, nullptr, &pool_),
            "create glyph command pool");
        auto semaphore = info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        auto fence = info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        auto allocation =
            info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        allocation.commandPool = pool_;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        for (uint32_t i = 0; i < count; ++i) {
            auto& frame = frames_[i];
            frame.image = images[i];
            resources_.texture(frame.capture, capture_format(format_), extent_);
            frame.output_view = resources_.image_view(frame.image, format_);
            pipeline_->set_target(i, frame.capture.view, frame.output_view);
            check(api_.CreateSemaphore(device_.handle, &semaphore, nullptr, &frame.completed),
                "create glyph present semaphore");
            check(api_.CreateFence(device_.handle, &fence, nullptr, &frame.fence),
                "create glyph fence");
            check(api_.AllocateCommandBuffers(device_.handle, &allocation, &frame.command),
                "allocate glyph command buffer");
            check(device_.set_loader_data(device_.handle, frame.command),
                "initialize layer command-buffer dispatch");
        }
        diagnostic("glyphshift: Vulkan glyph renderer initialized\n");
    }

    VkSemaphore draw(VkQueue queue,
        uint32_t index,
        const VkPresentInfoKHR& present,
        const Settings& settings,
        const ImDrawData* ui) {
        // Storage for wait masks is allocated once with the renderer. Refuse an
        // unexpectedly huge wait list instead of allocating in the frame loop.
        if (queue != queue_ || index >= frames_.size() ||
            present.waitSemaphoreCount > wait_masks_.size())
            return VK_NULL_HANDLE;
        const std::lock_guard lock(mutex_);
        auto& frame = frames_[index];
        check(api_.WaitForFences(device_.handle, 1, &frame.fence, VK_TRUE, UINT64_MAX),
            "wait for glyph frame");
        check(api_.ResetCommandBuffer(frame.command, 0), "reset glyph command buffer");
        auto begin = info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(api_.BeginCommandBuffer(frame.command, &begin), "begin glyph command buffer");
        // Capture: copy the presented image, then render over it.
        resources_.barrier(frame.command,
            frame.image,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            0,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
        resources_.barrier(frame.command,
            frame.capture.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.dstSubresource = copy.srcSubresource;
        copy.extent = {extent_.width, extent_.height, 1};
        api_.CmdCopyImage(frame.command,
            frame.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            frame.capture.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copy);
        resources_.barrier(frame.command,
            frame.capture.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        resources_.barrier(frame.command,
            frame.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        pipeline_->record(frame.command, index, settings, ui);
        check(api_.EndCommandBuffer(frame.command), "end glyph command buffer");
        auto submit = info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.waitSemaphoreCount = present.waitSemaphoreCount;
        submit.pWaitSemaphores = present.pWaitSemaphores;
        submit.pWaitDstStageMask = wait_masks_.data();
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &frame.command;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &frame.completed;
        check(api_.ResetFences(device_.handle, 1, &frame.fence), "reset glyph fence");
        check(api_.QueueSubmit(queue_, 1, &submit, frame.fence), "submit glyph work");
        submitted_ = true;
        return frame.completed;
    }

private:
    DeviceDispatch& device_;
    const RenderApi& api_;
    Resources resources_;
    VkSwapchainKHR swapchain_;
    VkExtent2D extent_;
    VkFormat format_;
    VkQueue queue_;
    QueueState queue_state_;
    CharacterSelection selection_;
    std::unique_ptr<Pipeline> pipeline_;
    std::mutex mutex_;
    bool submitted_ = false;
    std::vector<Frame> frames_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    const std::array<VkPipelineStageFlags, 64> wait_masks_ = [] {
        std::array<VkPipelineStageFlags, 64> values{};
        values.fill(VK_PIPELINE_STAGE_TRANSFER_BIT);
        return values;
    }();
};

VkResult render_present(DeviceDispatch& device, VkQueue queue, const VkPresentInfoKHR* info) {
    if (!device.render_api.available)
        return device.present(queue, info);
    auto forward = [&] {
        for (uint32_t i = 0; i < info->swapchainCount; ++i) {
            std::shared_ptr<SwapchainState> state;
            {
                const std::lock_guard lock(device.mutex);
                const auto found = device.swapchains.find(info->pSwapchains[i]);
                if (found != device.swapchains.end())
                    state = found->second;
            }
            if (state)
                editor::deactivate(state->display, state->window);
        }
        return device.present(queue, info);
    };
    if (info->swapchainCount != 1 || !supported_present_chain(info->pNext) ||
        info->waitSemaphoreCount > 64) {
        if (!device.render_warning.test_and_set(std::memory_order_relaxed)) {
            diagnostic(
                "glyphshift: unsupported Vulkan present batch/pNext; forwarding "
                "unchanged (sTypes:");
            const auto* extension = static_cast<const VkBaseInStructure*>(info->pNext);
            for (unsigned i = 0; extension && i < 8; ++i, extension = extension->pNext) {
                char type[32];
                std::snprintf(type, sizeof(type), " %d", static_cast<int>(extension->sType));
                diagnostic(type);
            }
            diagnostic(")\n");
        }
        return forward();
    }
    std::shared_ptr<SwapchainState> state;
    QueueState queue_state;
    {
        const std::lock_guard lock(device.mutex);
        const auto found = device.swapchains.find(info->pSwapchains[0]);
        const auto known_queue = device.queues.find(queue);
        if (found != device.swapchains.end() && known_queue != device.queues.end()) {
            state = found->second;
            queue_state = known_queue->second;
        }
    }
    if (!state || !state->capture || !capture_format(state->format)) {
        warning(device,
            "glyphshift: unsupported Vulkan capture usage/format; forwarding "
            "unchanged\n");
        return forward();
    }
    const bool ownership =
        state->sharing == VK_SHARING_MODE_CONCURRENT
            ? std::find(state->families.begin(),
                  state->families.begin() + state->family_count,
                  queue_state.family) != state->families.begin() + state->family_count
            : state->sharing == VK_SHARING_MODE_EXCLUSIVE;
    // vkQueuePresentKHR requires its queue family to own an exclusive image.
    // Submit on that same queue and wait the original semaphores: other device
    // queue families do not invalidate this ownership contract. Graphics support
    // is checked before allocating the renderer's command pool.
    if (!ownership || queue_state.flags) {
        warning(device,
            "glyphshift: unsupported Vulkan queue ownership/protection; "
            "forwarding unchanged\n");
        return forward();
    }
    const auto start = std::chrono::steady_clock::now();
    editor::Frame editor_frame(state->display,
        state->window,
        state->extent.width,
        state->extent.height,
        "Vulkan",
        device.version.data(),
        display::x11::editor_input());
    if (!editor_frame.settings.enabled && !editor_frame.draw_data) {
        {
            // Frames presented unprocessed break the cells' temporal continuity.
            const std::lock_guard lock(state->mutex);
            if (state->renderer)
                state->renderer->forget_history();
        }
        editor_frame.complete(0);
        return device.present(queue, info);
    }
    std::shared_ptr<GlyphRenderer> renderer, retired;
    bool initialize = false, blocked = false;
    {
        const std::lock_guard lock(state->mutex);
        blocked = state->render_failed || state->initializing;
        if (state->renderer && !state->renderer->matches(editor_frame.settings.glyphs))
            retired = std::move(state->renderer);
        renderer = state->renderer;
        if (!blocked && !renderer) {
            state->initializing = initialize = true;
        }
    }
    if (blocked) {
        editor_frame.complete(0);
        editor::deactivate(state->display, state->window);
        return forward();
    }
    retired.reset();
    try {
        if (initialize) {
            renderer = std::make_shared<GlyphRenderer>(device,
                info->pSwapchains[0],
                *state,
                queue,
                queue_state,
                editor_frame.settings.glyphs);
            renderer->initialize();
            const std::lock_guard lock(state->mutex);
            state->renderer = renderer;
            state->initializing = false;
        }
        const auto completed = renderer->draw(queue,
            info->pImageIndices[0],
            *info,
            editor_frame.settings,
            editor_frame.draw_data);
        editor_frame.complete(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
        if (!completed) {
            warning(device,
                "glyphshift: unsupported Vulkan queue change/image index; "
                "forwarding unchanged\n");
            return forward();
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
        return forward();
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
