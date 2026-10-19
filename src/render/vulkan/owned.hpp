#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <functional>
#include <glyphshift/config/settings.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "pipeline.hpp"
#include "resources.hpp"

struct ImDrawData;

namespace glyphshift::vulkan {
// A source image that arrives from outside the renderer (a window capture),
// shared with its producer through a timeline semaphore: each use waits for
// one value and signals the next, so the producer does not overwrite the image
// while a frame still reads it.
class ExternalSource {
public:
    struct Use {
        uint64_t wait = 0, signal = 0;
    };

    virtual ~ExternalSource() = default;
    virtual VkImageView view() const = 0;
    virtual VkExtent2D extent() const = 0;
    virtual VkSemaphore semaphore() const = 0;
    // Leaves the image in SHADER_READ_ONLY_OPTIMAL, visible to fragment shaders.
    virtual void acquire(VkCommandBuffer command) const = 0;
    // Returns it to its producer after the frame's fragment-shader reads.
    virtual void release(VkCommandBuffer command) const = 0;
    // The values of the submission about to be made; called only right before
    // submitting, so every reserved value is signaled.
    virtual Use use() = 0;
};

// Glyph rendering in a Vulkan device and window this process owns (the
// capture overlay): its own instance, device, swapchain and pipeline. The
// window system supplies the surface. GLYPHSHIFT_VULKAN_VALIDATION=1 enables
// Khronos validation with synchronization checks; findings go to stderr as
// "Vulkan validation: ...", the loader's messages as "Vulkan loader: ...".
class OwnedRenderer {
public:
    struct Options {
        // Instance extensions of the window system's surface.
        std::vector<const char*> instance_extensions;
        // Device extensions the source needs (for example external memory).
        std::vector<const char*> device_extensions;
        bool timeline_semaphores = false;
    };

    OwnedRenderer(const Options& options,
        const std::function<VkSurfaceKHR(VkInstance)>& make_surface);
    ~OwnedRenderer();
    OwnedRenderer(const OwnedRenderer&) = delete;
    OwnedRenderer& operator=(const OwnedRenderer&) = delete;

    VkInstance instance() const noexcept {
        return instance_;
    }

    VkPhysicalDevice physical() const noexcept {
        return physical_;
    }

    VkDevice device() const noexcept {
        return device_;
    }

    const Resources& resources() const noexcept {
        return *resources_;
    }

    uint32_t family() const noexcept {
        return family_;
    }

    // The device's locally unique identifier (all zero when unknown), to
    // match another API's adapter.
    const std::array<uint8_t, 8>& luid() const noexcept {
        return luid_;
    }

    // Waits until the device has finished every submitted frame (before
    // replacing a source the frames read).
    void wait_idle() const {
        check(vkDeviceWaitIdle(device_), "wait for the device");
    }

    // "Vulkan 1.4.325, GPU name", for the editor panel.
    const char* version() const noexcept {
        return version_.c_str();
    }

    // Renders one frame of `source` into the window and presents it: the glyph
    // effect, or with the effect disabled (F10 with the editor open) the
    // original image, then the editor. The swapchain follows `extent`, the
    // window's size. Returns false when nothing was presented (zero size,
    // source of another size, or an out-of-date swapchain to recreate).
    bool draw(ExternalSource& source,
        VkExtent2D extent,
        const Settings& settings,
        const ImDrawData* ui);

private:
    struct Frame {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    void release() noexcept;
    void select_device(const Options& options);
    void recreate(VkExtent2D extent);
    void destroy_swapchain() noexcept;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    std::array<uint8_t, 8> luid_{};
    std::string version_;
    RenderApi api_;
    std::unique_ptr<Resources> resources_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::array<Frame, 2> frames_{};
    uint64_t frame_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    // Present waits on one semaphore per swapchain image, reused only after
    // that image is acquired again.
    std::vector<VkSemaphore> rendered_;
    std::unique_ptr<Pipeline> pipeline_;
    VkImageView bound_source_ = VK_NULL_HANDLE;
    CharacterSelection selection_{};
    bool stale_ = false;
};
}  // namespace glyphshift::vulkan
