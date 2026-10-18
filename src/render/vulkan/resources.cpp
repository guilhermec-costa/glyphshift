#include "resources.hpp"

namespace glyphshift::vulkan {
RenderApi load_render_api(PFN_vkGetDeviceProcAddr get_proc, VkDevice device) {
    RenderApi api;
    if (!get_proc)
        return api;
    api.available = true;
#define LOAD(name)                                                           \
    api.name = reinterpret_cast<PFN_vk##name>(get_proc(device, "vk" #name)); \
    api.available &= api.name != nullptr;
    GLYPHSHIFT_VK_RENDER_FUNCTIONS(LOAD)
#undef LOAD
    return api;
}

Resources::Resources(const Device& device) : device_(device) {
    if (!device_.api || !device_.api->available || !device_.memory_properties ||
        !device_.format_properties)
        throw std::runtime_error("Vulkan device functions unavailable");
    device_.memory_properties(device_.physical, &memory_);
}

VkFormatFeatureFlags Resources::features(VkFormat format) const {
    VkFormatProperties properties{};
    device_.format_properties(device_.physical, format, &properties);
    return properties.optimalTilingFeatures;
}

void Resources::allocate(VkMemoryRequirements requirements,
    VkMemoryPropertyFlags flags,
    VkDeviceMemory& output) const {
    uint32_t type = memory_.memoryTypeCount;
    for (uint32_t i = 0; i < memory_.memoryTypeCount && type == memory_.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1U << i)) &&
            (memory_.memoryTypes[i].propertyFlags & flags) == flags)
            type = i;
    if (type == memory_.memoryTypeCount)
        throw std::runtime_error("no compatible glyph memory type");
    auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    check(api().AllocateMemory(device_.handle, &allocation, nullptr, &output),
        "allocate glyph memory");
}

VkImageView Resources::image_view(VkImage image, VkFormat format) const {
    auto view = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view.image = image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView result{};
    check(api().CreateImageView(device_.handle, &view, nullptr, &result),
        "create glyph image view");
    return result;
}

void Resources::texture(Texture& target,
    VkFormat format,
    VkExtent2D extent,
    VkImageUsageFlags usage) const {
    auto image = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {extent.width, extent.height, 1};
    image.mipLevels = image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = usage;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(api().CreateImage(device_.handle, &image, nullptr, &target.image),
        "create glyph texture");
    VkMemoryRequirements requirements{};
    api().GetImageMemoryRequirements(device_.handle, target.image, &requirements);
    allocate(requirements, 0, target.memory);
    check(api().BindImageMemory(device_.handle, target.image, target.memory, 0),
        "bind glyph texture memory");
    target.view = image_view(target.image, format);
}

void Resources::destroy(Texture& texture) const {
    if (texture.view)
        api().DestroyImageView(device_.handle, texture.view, nullptr);
    if (texture.image)
        api().DestroyImage(device_.handle, texture.image, nullptr);
    if (texture.memory)
        api().FreeMemory(device_.handle, texture.memory, nullptr);
    texture = {};
}

void Resources::barrier(VkCommandBuffer command,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkAccessFlags source,
    VkAccessFlags destination,
    VkPipelineStageFlags source_stage,
    VkPipelineStageFlags destination_stage) const {
    auto barrier = info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.srcAccessMask = source;
    barrier.dstAccessMask = destination;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    api().CmdPipelineBarrier(command,
        source_stage,
        destination_stage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}
}  // namespace glyphshift::vulkan
