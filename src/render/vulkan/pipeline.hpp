#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <glyphshift/config/characters.hpp>
#include <glyphshift/config/settings.hpp>
#include <vector>

#include "resources.hpp"

struct ImDrawData;

namespace glyphshift::vulkan {
// The Vulkan glyph pipeline, independent of how images arrive: the analysis
// and composition passes, the glyph atlas, the two cell grids and their
// history, and editor drawing. A target pairs a source image (sampled) with an
// output image (rendered); the host records the source's writes and submits.
// The loader layer uses one target per swapchain image; a capture overlay
// would use its imported frames.
class Pipeline {
public:
    // Builds the atlas for `characters` and every device object for `targets`
    // targets of `extent`, rendering in `format` (8-bit RGBA/BGRA, UNORM or
    // SRGB) and leaving outputs in `final_layout`. Throws std::runtime_error.
    Pipeline(const Resources& resources,
        VkFormat format,
        VkExtent2D extent,
        VkImageLayout final_layout,
        unsigned targets,
        const CharacterSelection& characters);
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    bool matches(const CharacterSelection& value) const {
        return selection_ == value;
    }

    // The atlas diagnostic (a fallback to the classic set), empty if none.
    const char* atlas_diagnostic() const noexcept {
        return atlas_data_.diagnostic.data();
    }

    // Frames shown unprocessed break the cells' temporal continuity.
    void forget_history() noexcept {
        history_ = false;
    }

    // Target `index` samples `source` and renders into `output`, a view of an
    // image of the pipeline's format and extent.
    void set_target(unsigned index, VkImageView source, VkImageView output);

    // Records one frame of target `index` into `command`. On entry the source
    // is in SHADER_READ_ONLY_OPTIMAL with its writes made visible to fragment
    // shaders, and the output is in COLOR_ATTACHMENT_OPTIMAL; the output ends
    // in the final layout. With the effect disabled only the editor is drawn
    // over the output. Copies editor geometry on the host: call only once the
    // target's previous submission has completed.
    void record(VkCommandBuffer command,
        unsigned index,
        const Settings& settings,
        const ImDrawData* ui);

private:
    struct Target {
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkBuffer ui_buffer = VK_NULL_HANDLE;
        VkDeviceMemory ui_memory = VK_NULL_HANDLE;
        void* ui_mapped = nullptr;
        VkDeviceSize ui_capacity = 0;
        // This target's art parameters (uniform buffer, binding 5).
        VkBuffer art_buffer = VK_NULL_HANDLE;
        VkDeviceMemory art_memory = VK_NULL_HANDLE;
        void* art_mapped = nullptr;
        // Binding 4 of set i reads cell grid i: composition reads the grid just
        // written, analysis reads the other one (the previous frame's).
        std::array<VkDescriptorSet, 2> descriptors{};
    };

    void release() noexcept;
    void staging();
    void descriptors(unsigned targets);
    void pipelines();
    void analysis_pipeline(VkGraphicsPipelineCreateInfo create,
        VkPipelineShaderStageCreateInfo& fragment);
    void art_buffer(Target& target);
    void ensure_ui_buffer(Target& target, VkDeviceSize required);
    void upload_ui(Target& target, const ImDrawData& data);
    void draw_ui(VkCommandBuffer command, const Target& target, const ImDrawData& data);
    void upload(VkCommandBuffer command,
        const Texture& texture,
        VkDeviceSize offset,
        VkExtent2D extent);

    const Resources& resources_;
    const RenderApi& api_;
    VkDevice device_;
    VkFormat format_;
    VkExtent2D extent_;
    VkImageLayout final_layout_;
    CharacterSelection selection_;
    CharacterAtlas atlas_data_;
    bool uploaded_ = false;
    // Cell grid written by the latest enabled frame, and whether it is usable
    // history for the next one (cleared when frames pass through unprocessed).
    unsigned current_ = 0;
    std::atomic<bool> history_{false};
    Settings history_settings_{};
    std::vector<Target> targets_;
    Texture atlas_, ramp_, font_;
    std::array<Texture, 2> cells_;
    VkExtent2D font_extent_{}, cell_extent_{};
    static constexpr VkFormat cell_format = VK_FORMAT_R32G32B32A32_UINT;
    static constexpr VkDeviceSize font_offset_ =
        (integral_bytes + 256 * ramp_height + 3) & ~VkDeviceSize{3};
    VkBuffer staging_ = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE, analysis_pass_ = VK_NULL_HANDLE;
    std::array<VkFramebuffer, 2> cell_framebuffers_{};
    VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE, analysis_pipeline_ = VK_NULL_HANDLE;
    std::array<VkShaderModule, 5> modules_{};
};
}  // namespace glyphshift::vulkan
