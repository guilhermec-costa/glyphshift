#pragma once
#include <windows.h>
// After windows.h; the build defines VK_USE_PLATFORM_WIN32_KHR on Windows.
#include <vulkan/vulkan.h>

#include <cstdint>

#include "owned.hpp"
#include "resources.hpp"

// Windows interop of the Vulkan renderer: a D3D11 texture shared through an NT
// handle (VK_KHR_external_memory_win32) and a shared D3D11 fence imported as a
// timeline semaphore (VK_KHR_external_semaphore_win32), sampled as the glyph
// pipeline's source. The handles stay owned by their producer.
namespace glyphshift::vulkan {
class Win32Import final : public ExternalSource {
public:
    // Device extensions the import needs, for OwnedRenderer::Options.
    static constexpr const char* extensions[] = {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};

    // `value` is the shared fence's last reserved value, kept by the producer;
    // each use waits for it and signals the next one.
    Win32Import(const OwnedRenderer& renderer, uint64_t& value);
    ~Win32Import() override;
    Win32Import(const Win32Import&) = delete;
    Win32Import& operator=(const Win32Import&) = delete;

    // Imports a B8G8R8A8_UNORM texture of `extent`, replacing the previous
    // one; the device must not be using it.
    void import_texture(HANDLE texture, VkExtent2D extent);
    // Imports the producer's fence (once).
    void import_fence(HANDLE fence);

    VkImageView view() const override {
        return texture_.view;
    }

    VkExtent2D extent() const override {
        return extent_;
    }

    VkSemaphore semaphore() const override {
        return semaphore_;
    }

    void acquire(VkCommandBuffer command) const override;
    void release(VkCommandBuffer command) const override;
    Use use() override;

private:
    void release_texture() noexcept;

    const OwnedRenderer& renderer_;
    VkDevice device_;
    uint64_t& value_;
    Texture texture_;
    VkExtent2D extent_{};
    VkSemaphore semaphore_ = VK_NULL_HANDLE;
    PFN_vkGetMemoryWin32HandlePropertiesKHR memory_properties_ = nullptr;
    PFN_vkImportSemaphoreWin32HandleKHR import_semaphore_ = nullptr;
};
}  // namespace glyphshift::vulkan
