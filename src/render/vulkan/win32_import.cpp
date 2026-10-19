#include "win32_import.hpp"

#include <stdexcept>

namespace glyphshift::vulkan {
namespace {
constexpr auto memory_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
constexpr VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
}  // namespace

Win32Import::Win32Import(const OwnedRenderer& renderer, uint64_t& value)
    : renderer_(renderer), device_(renderer.device()), value_(value) {
    memory_properties_ = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
        vkGetDeviceProcAddr(device_, "vkGetMemoryWin32HandlePropertiesKHR"));
    import_semaphore_ = reinterpret_cast<PFN_vkImportSemaphoreWin32HandleKHR>(
        vkGetDeviceProcAddr(device_, "vkImportSemaphoreWin32HandleKHR"));
    if (!memory_properties_ || !import_semaphore_)
        throw std::runtime_error("the Vulkan device lacks Win32 external memory or semaphores");
}

Win32Import::~Win32Import() {
    release_texture();
    if (semaphore_)
        vkDestroySemaphore(device_, semaphore_, nullptr);
}

void Win32Import::release_texture() noexcept {
    if (texture_.view)
        vkDestroyImageView(device_, texture_.view, nullptr);
    if (texture_.image)
        vkDestroyImage(device_, texture_.image, nullptr);
    if (texture_.memory)
        vkFreeMemory(device_, texture_.memory, nullptr);
    texture_ = {};
    extent_ = {};
}

void Win32Import::import_texture(HANDLE texture, VkExtent2D extent) {
    release_texture();
    auto external =
        info<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    external.handleTypes = memory_type;
    auto image = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    image.pNext = &external;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {extent.width, extent.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device_, &image, nullptr, &texture_.image), "create imported image");
    auto properties = info<VkMemoryWin32HandlePropertiesKHR>(
        VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR);
    check(memory_properties_(device_, memory_type, texture, &properties),
        "query the shared texture's memory");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, texture_.image, &requirements);
    requirements.memoryTypeBits &= properties.memoryTypeBits;
    if (!requirements.memoryTypeBits)
        throw std::runtime_error("no memory type can import the shared texture");
    // The driver requires a dedicated allocation for D3D11 textures.
    auto dedicated =
        info<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
    dedicated.image = texture_.image;
    auto import = info<VkImportMemoryWin32HandleInfoKHR>(
        VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR);
    import.pNext = &dedicated;
    import.handleType = memory_type;
    import.handle = texture;
    uint32_t type = 0;
    while (!(requirements.memoryTypeBits & (1U << type)))
        ++type;
    auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.pNext = &import;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    check(vkAllocateMemory(device_, &allocation, nullptr, &texture_.memory),
        "import the shared texture");
    check(vkBindImageMemory(device_, texture_.image, texture_.memory, 0),
        "bind the shared texture");
    texture_.view = renderer_.resources().image_view(texture_.image, format);
    extent_ = extent;
}

void Win32Import::import_fence(HANDLE fence) {
    if (semaphore_)
        return;
    auto type = info<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    auto create = info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    create.pNext = &type;
    check(vkCreateSemaphore(device_, &create, nullptr, &semaphore_), "create shared semaphore");
    auto import = info<VkImportSemaphoreWin32HandleInfoKHR>(
        VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR);
    import.semaphore = semaphore_;
    import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE_BIT;
    import.handle = fence;
    check(import_semaphore_(device_, &import), "import the shared fence");
}

// The image arrives from the external owner (D3D11) in its general layout.
void Win32Import::acquire(VkCommandBuffer command) const {
    auto barrier = info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    barrier.dstQueueFamilyIndex = renderer_.family();
    barrier.image = texture_.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}

void Win32Import::release(VkCommandBuffer command) const {
    auto barrier = info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = renderer_.family();
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    barrier.image = texture_.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}

ExternalSource::Use Win32Import::use() {
    const Use use{value_, value_ + 1};
    ++value_;
    return use;
}
}  // namespace glyphshift::vulkan
