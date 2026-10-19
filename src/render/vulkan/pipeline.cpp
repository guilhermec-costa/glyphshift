#include "pipeline.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <glyphshift/core/glyph.hpp>
#include <glyphshift/editor/editor.hpp>
#include <span>

#include "editor_frag_spv.hpp"
#include "editor_vert_spv.hpp"
#include "glyph_analysis_spv.hpp"
#include "glyph_frag_spv.hpp"
#include "glyph_luma_spv.hpp"
#include "glyph_meter_spv.hpp"
#include "glyph_motion_spv.hpp"
#include "glyph_resolve_spv.hpp"
#include "glyph_vert_spv.hpp"

namespace glyphshift::vulkan {
namespace {
// Push constants; the art parameters travel in a per-target uniform buffer.
struct Parameters {
    float width, height, cell_width, cell_height;
    uint32_t source_color, ramp_count, output_srgb;
    float glyph_strength;
};

// Vulkan 1.0 guarantees 128 bytes of push constants.
static_assert(sizeof(Parameters) <= 128);
// The editor's projection shares the same push-constant range.
static_assert(sizeof(Parameters) >= 20);
// std140: an array of vec4 is tightly packed.
constexpr VkDeviceSize art_bytes = sizeof(ArtParameters);

bool srgb(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
}
}  // namespace

Pipeline::Pipeline(const Resources& resources,
    VkFormat format,
    VkExtent2D extent,
    VkImageLayout final_layout,
    unsigned targets,
    const CharacterSelection& characters)
    : resources_(resources),
      api_(resources.api()),
      device_(resources.device().handle),
      format_(format),
      extent_(extent),
      final_layout_(final_layout),
      selection_(characters) {
    try {
        atlas_data_ = build_character_atlas(selection_);
        editor::character_report(atlas_data_.diagnostic.data());
        if (!extent_.width || !extent_.height || !targets || targets > 256)
            throw std::runtime_error("unsupported glyph target extent/count");
        for (const auto texture : {VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UINT, VK_FORMAT_R32_SFLOAT})
            if (!(resources_.features(texture) & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
                throw std::runtime_error("atlas texture format cannot be sampled");
        constexpr auto cell_features =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        if ((resources_.features(cell_format) & cell_features) != cell_features)
            throw std::runtime_error("glyph cell format cannot be rendered and sampled");
        for (const auto pass_format : {meter_format, luma_format, camera_format})
            if ((resources_.features(pass_format) & cell_features) != cell_features)
                throw std::runtime_error("glyph pass format cannot be rendered and sampled");
        resources_.texture(atlas_, VK_FORMAT_R32_SFLOAT, {integral_width, integral_height});
        resources_.texture(ramp_, VK_FORMAT_R8_UINT, {256, ramp_height});
        const auto font = editor::font();
        font_extent_ = {static_cast<uint32_t>(font.width), static_cast<uint32_t>(font.height)};
        resources_.texture(font_, VK_FORMAT_R8_UNORM, font_extent_);
        // Sized for the smallest 2x2 cell, so live cell-size edits never
        // reallocate; each frame renders only the current grid.
        // One more column and row for an anchored grid's partial edge cells.
        cell_extent_ = {(extent_.width + 1) / 2 + 1, (extent_.height + 1) / 2 + 1};
        for (auto& cells : cells_)
            resources_.texture(cells,
                cell_format,
                cell_extent_,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        constexpr auto rendered = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        for (auto& meter : meters_)
            resources_.texture(meter, meter_format, {1, 1}, rendered);
        resources_.texture(luma_, luma_format, extent_, rendered);
        resources_.texture(costs_, meter_format, {candidates, candidates}, rendered);
        for (auto& camera : cameras_)
            resources_.texture(camera, camera_format, {2, 1}, rendered);
        staging();
        descriptors(targets);
        pipelines();
        for (unsigned i = 0; i < 2; ++i) {
            meter_framebuffers_[i] = framebuffer(meter_pass_, meters_[i], {1, 1});
            camera_framebuffers_[i] = framebuffer(camera_pass_, cameras_[i], {2, 1});
        }
        luma_framebuffer_ = framebuffer(luma_pass_, luma_, extent_);
        costs_framebuffer_ = framebuffer(meter_pass_, costs_, {candidates, candidates});
        for (unsigned i = 0; i < 2; ++i) {
            auto cell_framebuffer =
                info<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
            cell_framebuffer.renderPass = analysis_pass_;
            cell_framebuffer.attachmentCount = 1;
            cell_framebuffer.pAttachments = &cells_[i].view;
            cell_framebuffer.width = cell_extent_.width;
            cell_framebuffer.height = cell_extent_.height;
            cell_framebuffer.layers = 1;
            check(
                api_.CreateFramebuffer(device_, &cell_framebuffer, nullptr, &cell_framebuffers_[i]),
                "create glyph cell framebuffer");
        }
        targets_.resize(targets);
        for (auto& target : targets_) {
            ensure_ui_buffer(target, 256 * 1024);
            art_buffer(target);
            for (auto& set : target.descriptors) {
                auto descriptor = info<VkDescriptorSetAllocateInfo>(
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
                descriptor.descriptorPool = descriptor_pool_;
                descriptor.descriptorSetCount = 1;
                descriptor.pSetLayouts = &descriptor_layout_;
                check(api_.AllocateDescriptorSets(device_, &descriptor, &set),
                    "allocate glyph descriptors");
            }
        }
    } catch (...) {
        release();
        throw;
    }
}

Pipeline::~Pipeline() {
    release();
}

void Pipeline::release() noexcept {
    if (descriptor_pool_)
        api_.DestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    descriptor_pool_ = VK_NULL_HANDLE;
    for (auto& target : targets_) {
        if (target.framebuffer)
            api_.DestroyFramebuffer(device_, target.framebuffer, nullptr);
        if (target.ui_mapped)
            api_.UnmapMemory(device_, target.ui_memory);
        if (target.ui_buffer)
            api_.DestroyBuffer(device_, target.ui_buffer, nullptr);
        if (target.ui_memory)
            api_.FreeMemory(device_, target.ui_memory, nullptr);
        if (target.art_mapped)
            api_.UnmapMemory(device_, target.art_memory);
        if (target.art_buffer)
            api_.DestroyBuffer(device_, target.art_buffer, nullptr);
        if (target.art_memory)
            api_.FreeMemory(device_, target.art_memory, nullptr);
    }
    targets_.clear();
    for (auto* pipeline : {&ui_pipeline_,
             &pipeline_,
             &analysis_pipeline_,
             &meter_pipeline_,
             &motion_pipeline_,
             &resolve_pipeline_,
             &luma_pipeline_}) {
        if (*pipeline)
            api_.DestroyPipeline(device_, *pipeline, nullptr);
        *pipeline = VK_NULL_HANDLE;
    }
    for (auto* framebuffers : {&cell_framebuffers_, &meter_framebuffers_, &camera_framebuffers_})
        for (auto& framebuffer : *framebuffers) {
            if (framebuffer)
                api_.DestroyFramebuffer(device_, framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }
    for (auto* framebuffer : {&luma_framebuffer_, &costs_framebuffer_}) {
        if (*framebuffer)
            api_.DestroyFramebuffer(device_, *framebuffer, nullptr);
        *framebuffer = VK_NULL_HANDLE;
    }
    for (auto* pass : {&analysis_pass_, &meter_pass_, &luma_pass_, &camera_pass_, &render_pass_}) {
        if (*pass)
            api_.DestroyRenderPass(device_, *pass, nullptr);
        *pass = VK_NULL_HANDLE;
    }
    for (auto& module : modules_) {
        if (module)
            api_.DestroyShaderModule(device_, module, nullptr);
        module = VK_NULL_HANDLE;
    }
    if (layout_)
        api_.DestroyPipelineLayout(device_, layout_, nullptr);
    layout_ = VK_NULL_HANDLE;
    if (descriptor_layout_)
        api_.DestroyDescriptorSetLayout(device_, descriptor_layout_, nullptr);
    descriptor_layout_ = VK_NULL_HANDLE;
    if (sampler_)
        api_.DestroySampler(device_, sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    resources_.destroy(font_);
    resources_.destroy(cells_[0]);
    resources_.destroy(cells_[1]);
    for (auto* texture : {&meters_[0], &meters_[1], &luma_, &costs_, &cameras_[0], &cameras_[1]})
        resources_.destroy(*texture);
    resources_.destroy(atlas_);
    resources_.destroy(ramp_);
    if (staging_)
        api_.DestroyBuffer(device_, staging_, nullptr);
    staging_ = VK_NULL_HANDLE;
    if (staging_memory_)
        api_.FreeMemory(device_, staging_memory_, nullptr);
    staging_memory_ = VK_NULL_HANDLE;
}

void Pipeline::set_target(unsigned index, VkImageView source, VkImageView output) {
    auto& target = targets_.at(index);
    if (target.framebuffer)
        api_.DestroyFramebuffer(device_, target.framebuffer, nullptr);
    target.framebuffer = VK_NULL_HANDLE;
    auto framebuffer = info<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    framebuffer.renderPass = render_pass_;
    framebuffer.attachmentCount = 1;
    framebuffer.pAttachments = &output;
    framebuffer.width = extent_.width;
    framebuffer.height = extent_.height;
    framebuffer.layers = 1;
    check(api_.CreateFramebuffer(device_, &framebuffer, nullptr, &target.framebuffer),
        "create glyph framebuffer");
    for (unsigned set = 0; set < 2; ++set) {
        // Set s is bound while grid s holds the previous frame: the meter then
        // reads meters[s] and writes meters[s ^ 1], which analysis reads.
        const std::array<VkDescriptorImageInfo, 12> bindings{
            {{sampler_, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {sampler_, atlas_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {sampler_, ramp_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {sampler_, font_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {sampler_, cells_[set].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {},
                {sampler_, meters_[set].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {sampler_, meters_[set ^ 1U].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {sampler_, luma_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {sampler_, costs_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {sampler_, cameras_[set].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {sampler_, cameras_[set ^ 1U].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
        const VkDescriptorBufferInfo art{target.art_buffer, 0, art_bytes};
        std::array<VkWriteDescriptorSet, 12> writes{};
        for (uint32_t binding = 0; binding < writes.size(); ++binding) {
            writes[binding] = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            writes[binding].dstSet = target.descriptors[set];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            if (binding == 5) {
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[binding].pBufferInfo = &art;
            } else {
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[binding].pImageInfo = &bindings[binding];
            }
        }
        api_.UpdateDescriptorSets(device_,
            static_cast<uint32_t>(writes.size()),
            writes.data(),
            0,
            nullptr);
    }
}

void Pipeline::record(VkCommandBuffer command,
    unsigned index,
    const Settings& settings,
    const ImDrawData* ui) {
    auto& target = targets_.at(index);
    if (ui)
        upload_ui(target, *ui);
    if (!uploaded_) {
        upload(command, atlas_, 0, {integral_width, integral_height});
        upload(command, ramp_, integral_bytes, {256, ramp_height});
        upload(command, font_, font_offset_, font_extent_);
        // Analysis statically reads the previous grid even when history is
        // unused, so both grids start in the layout its descriptor declares.
        for (const auto* image : {&cells_[0],
                 &cells_[1],
                 &meters_[0],
                 &meters_[1],
                 &luma_,
                 &costs_,
                 &cameras_[0],
                 &cameras_[1]})
            resources_.barrier(command,
                image->image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                0,
                VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        uploaded_ = true;
    }
    const bool anchoring = settings.enabled && settings.art.illustrated && settings.art.anchor_grid;
    // An anchored grid adds a column and a row for the partial edge cells.
    const uint32_t extra = anchoring ? 1 : 0;
    const VkExtent2D grid{
        std::min(cell_extent_.width,
            (extent_.width + settings.cell_width - 1) / settings.cell_width + extra),
        std::min(cell_extent_.height,
            (extent_.height + settings.cell_height - 1) / settings.cell_height + extra)};
    // History is the other grid, valid only after an illustrated frame
    // with identical settings (grid, glyph atlas and every art control).
    const bool history =
        history_ && settings.enabled && settings.art.illustrated && history_settings_ == settings;
    if (settings.enabled)
        current_ ^= 1U;
    history_ = settings.enabled && settings.art.illustrated;
    history_settings_ = settings;
    const Parameters parameters{static_cast<float>(extent_.width),
        static_cast<float>(extent_.height),
        static_cast<float>(settings.cell_width),
        static_cast<float>(settings.cell_height),
        settings.source_color ? 1U : 0U,
        atlas_data_.count,
        srgb(format_) ? 1U : 0U,
        settings.glyph_strength};
    auto art = art_parameters(settings);
    if (!history)
        art[art_stability] = 0;
    const bool metering =
        settings.enabled && settings.art.illustrated && settings.art.auto_exposure > 0;
    if (metering)
        art[art_adaptation] = adaptation();
    metered_ = metering;
    // The camera state continues while the cell size stays (the extent is
    // fixed for a pipeline).
    const bool anchor_history = anchoring && anchored_ &&
                                anchor_cell_width_ == settings.cell_width &&
                                anchor_cell_height_ == settings.cell_height;
    art[art_anchor_history] = anchor_history ? 1.0F : 0.0F;
    anchored_ = anchoring;
    anchor_cell_width_ = settings.cell_width;
    anchor_cell_height_ = settings.cell_height;
    std::memcpy(target.art_mapped, art.data(), art_bytes);
    api_.CmdPushConstants(command,
        layout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(parameters),
        &parameters);
    if (settings.enabled) {
        // The composition pass declares the cell image, so it is always
        // written (zeros in classic mode) and left shader-readable.
        api_.CmdBindDescriptorSets(command,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            layout_,
            0,
            1,
            &target.descriptors[current_ ^ 1U],
            0,
            nullptr);
        if (anchoring) {
            // Motion search against the previous luminance, then this frame's
            // luminance for the next search, then the camera state.
            if (anchor_history)
                offscreen(command,
                    meter_pass_,
                    costs_framebuffer_,
                    motion_pipeline_,
                    {candidates, candidates});
            offscreen(command, luma_pass_, luma_framebuffer_, luma_pipeline_, extent_);
            offscreen(command,
                camera_pass_,
                camera_framebuffers_[current_],
                resolve_pipeline_,
                {2, 1});
        }
        if (metering)
            offscreen(command, meter_pass_, meter_framebuffers_[current_], meter_pipeline_, {1, 1});
        auto analysis = info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
        analysis.renderPass = analysis_pass_;
        analysis.framebuffer = cell_framebuffers_[current_];
        analysis.renderArea.extent = grid;
        api_.CmdBeginRenderPass(command, &analysis, VK_SUBPASS_CONTENTS_INLINE);
        api_.CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, analysis_pipeline_);
        const VkViewport cells{0,
            0,
            static_cast<float>(grid.width),
            static_cast<float>(grid.height),
            0,
            1};
        const VkRect2D area{{0, 0}, grid};
        api_.CmdSetViewport(command, 0, 1, &cells);
        api_.CmdSetScissor(command, 0, 1, &area);
        api_.CmdDraw(command, 3, 1, 0, 0);
        api_.CmdEndRenderPass(command);
    }
    api_.CmdBindDescriptorSets(command,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        layout_,
        0,
        1,
        &target.descriptors[current_],
        0,
        nullptr);
    auto pass = info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    pass.renderPass = render_pass_;
    pass.framebuffer = target.framebuffer;
    pass.renderArea.extent = extent_;
    api_.CmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    api_.CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    const VkViewport viewport{0,
        0,
        static_cast<float>(extent_.width),
        static_cast<float>(extent_.height),
        0,
        1};
    const VkRect2D scissor{{0, 0}, extent_};
    api_.CmdSetViewport(command, 0, 1, &viewport);
    api_.CmdSetScissor(command, 0, 1, &scissor);
    if (settings.enabled)
        api_.CmdDraw(command, 3, 1, 0, 0);
    if (ui)
        draw_ui(command, target, *ui);
    api_.CmdEndRenderPass(command);
}

void Pipeline::staging() {
    const auto font = editor::font();
    const auto bytes = font_offset_ + static_cast<VkDeviceSize>(font.width) *
                                          static_cast<VkDeviceSize>(font.height);
    auto buffer = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    buffer.size = bytes;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(api_.CreateBuffer(device_, &buffer, nullptr, &staging_), "create atlas staging buffer");
    VkMemoryRequirements requirements{};
    api_.GetBufferMemoryRequirements(device_, staging_, &requirements);
    resources_.allocate(requirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_memory_);
    check(api_.BindBufferMemory(device_, staging_, staging_memory_, 0),
        "bind atlas staging memory");
    void* mapped = nullptr;
    check(api_.MapMemory(device_, staging_memory_, 0, bytes, 0, &mapped), "map atlas upload");
    std::memcpy(mapped, atlas_data_.integral.data(), integral_bytes);
    std::memcpy(static_cast<std::uint8_t*>(mapped) + integral_bytes,
        atlas_data_.ramp.data(),
        atlas_data_.ramp.size());
    std::memcpy(static_cast<std::uint8_t*>(mapped) + font_offset_,
        font.pixels,
        static_cast<std::size_t>(font.width) * static_cast<std::size_t>(font.height));
    api_.UnmapMemory(device_, staging_memory_);
}

void Pipeline::descriptors(unsigned targets) {
    // Bindings 0..4: source, atlas, ramp, editor font and cell grid; 5: art;
    // 6 and 7: exposure meters (the previous adapted value, then this frame's);
    // 8..11: previous luminance, motion costs and two camera states.
    std::array<VkDescriptorSetLayoutBinding, 12> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i)
        bindings[i] = {i,
            i == 5 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            nullptr};
    auto layout =
        info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = static_cast<uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    check(api_.CreateDescriptorSetLayout(device_, &layout, nullptr, &descriptor_layout_),
        "create glyph descriptor layout");
    const std::array<VkDescriptorPoolSize, 2> sizes{
        {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(targets * 2 * 11)},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(targets * 2)}}};
    auto pool = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.maxSets = static_cast<uint32_t>(targets * 2);
    pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    check(api_.CreateDescriptorPool(device_, &pool, nullptr, &descriptor_pool_),
        "create glyph descriptor pool");
    auto sampler = info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(api_.CreateSampler(device_, &sampler, nullptr, &sampler_), "create glyph sampler");
}

void Pipeline::pipelines() {
    const std::array<std::span<const uint32_t>, 9> code{shaders::glyph_vert_spv,
        shaders::glyph_frag_spv,
        shaders::editor_vert_spv,
        shaders::editor_frag_spv,
        shaders::glyph_analysis_spv,
        shaders::glyph_meter_spv,
        shaders::glyph_motion_spv,
        shaders::glyph_resolve_spv,
        shaders::glyph_luma_spv};
    for (unsigned i = 0; i < code.size(); ++i) {
        auto module = info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        module.codeSize = code[i].size_bytes();
        module.pCode = code[i].data();
        check(api_.CreateShaderModule(device_, &module, nullptr, &modules_[i]),
            "create glyph SPIR-V module");
    }
    const VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(Parameters)};
    auto layout = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &descriptor_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    check(api_.CreatePipelineLayout(device_, &layout, nullptr, &layout_),
        "create glyph pipeline layout");
    VkAttachmentDescription color{};
    color.format = format_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = final_layout_;
    const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    auto pass = info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    pass.attachmentCount = 1;
    pass.pAttachments = &color;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    check(api_.CreateRenderPass(device_, &pass, nullptr, &render_pass_),
        "create glyph render pass");
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (unsigned i = 0; i < stages.size(); ++i) {
        stages[i] = info<VkPipelineShaderStageCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
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
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    auto blend = info<VkPipelineColorBlendStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const std::array<VkDynamicState, 2> states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    auto dynamic = info<VkPipelineDynamicStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = static_cast<uint32_t>(states.size());
    dynamic.pDynamicStates = states.data();
    auto create =
        info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
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
    check(api_.CreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &create, nullptr, &pipeline_),
        "create glyph graphics pipeline");
    offscreen_pipeline(create,
        stages[1],
        cell_format,
        modules_[4],
        analysis_pass_,
        analysis_pipeline_);
    offscreen_pipeline(create, stages[1], meter_format, modules_[5], meter_pass_, meter_pipeline_);
    offscreen_pipeline(create, stages[1], meter_format, modules_[6], meter_pass_, motion_pipeline_);
    offscreen_pipeline(create,
        stages[1],
        camera_format,
        modules_[7],
        camera_pass_,
        resolve_pipeline_);
    offscreen_pipeline(create, stages[1], luma_format, modules_[8], luma_pass_, luma_pipeline_);
    stages[0].module = modules_[2];
    stages[1].module = modules_[3];
    const VkVertexInputBindingDescription binding{0,
        sizeof(ImDrawVert),
        VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 3> attributes{
        {{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)},
            {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)}}};
    vertex.vertexBindingDescriptionCount = 1;
    vertex.pVertexBindingDescriptions = &binding;
    vertex.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertex.pVertexAttributeDescriptions = attributes.data();
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    check(api_.CreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &create, nullptr, &ui_pipeline_),
        "create editor graphics pipeline");
}

// Per-cell analysis: one integer attachment, rewritten every target. The
// dependencies order it after the previous frame's composition reads and
// make its writes visible to this frame's composition.
void Pipeline::offscreen_pipeline(VkGraphicsPipelineCreateInfo create,
    VkPipelineShaderStageCreateInfo& fragment,
    VkFormat format,
    VkShaderModule module,
    VkRenderPass& pass_output,
    VkPipeline& pipeline_output) {
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    const std::array<VkSubpassDependency, 2> dependencies{
        {{VK_SUBPASS_EXTERNAL,
             0,
             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
             0,
             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
             0},
            {0,
                VK_SUBPASS_EXTERNAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                0}}};
    auto pass = info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = static_cast<uint32_t>(dependencies.size());
    pass.pDependencies = dependencies.data();
    if (!pass_output)
        check(api_.CreateRenderPass(device_, &pass, nullptr, &pass_output),
            "create glyph offscreen render pass");
    const auto compose = fragment.module;
    fragment.module = module;
    create.renderPass = pass_output;
    check(api_.CreateGraphicsPipelines(device_,
              VK_NULL_HANDLE,
              1,
              &create,
              nullptr,
              &pipeline_output),
        "create glyph offscreen pipeline");
    fragment.module = compose;
}

VkFramebuffer Pipeline::framebuffer(VkRenderPass pass, const Texture& texture, VkExtent2D extent) {
    auto create = info<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    create.renderPass = pass;
    create.attachmentCount = 1;
    create.pAttachments = &texture.view;
    create.width = extent.width;
    create.height = extent.height;
    create.layers = 1;
    VkFramebuffer result = VK_NULL_HANDLE;
    check(api_.CreateFramebuffer(device_, &create, nullptr, &result),
        "create glyph pass framebuffer");
    return result;
}

// One full-target draw of a single-attachment pass; descriptors and push
// constants are already bound.
void Pipeline::offscreen(VkCommandBuffer command,
    VkRenderPass pass,
    VkFramebuffer framebuffer,
    VkPipeline pipeline,
    VkExtent2D extent) {
    auto begin = info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    begin.renderPass = pass;
    begin.framebuffer = framebuffer;
    begin.renderArea.extent = extent;
    api_.CmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    api_.CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{0,
        0,
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0,
        1};
    const VkRect2D area{{0, 0}, extent};
    api_.CmdSetViewport(command, 0, 1, &viewport);
    api_.CmdSetScissor(command, 0, 1, &area);
    api_.CmdDraw(command, 3, 1, 0, 0);
    api_.CmdEndRenderPass(command);
}

// Share of the way toward this frame's measurement: 1 without a previous value.
float Pipeline::adaptation() {
    const auto now = std::chrono::steady_clock::now();
    const double seconds = frame_interval_ > 0
                               ? frame_interval_
                               : std::chrono::duration<double>(now - meter_time_).count();
    meter_time_ = now;
    return metered_ ? exposure_adaptation(static_cast<float>(seconds)) : 1.0F;
}

// Host-visible and coherent, written while recording each frame: the
// target's previous submission has completed by then.
void Pipeline::art_buffer(Target& target) {
    auto buffer = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    buffer.size = art_bytes;
    buffer.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(api_.CreateBuffer(device_, &buffer, nullptr, &target.art_buffer),
        "create art parameter buffer");
    VkMemoryRequirements requirements{};
    api_.GetBufferMemoryRequirements(device_, target.art_buffer, &requirements);
    resources_.allocate(requirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        target.art_memory);
    check(api_.BindBufferMemory(device_, target.art_buffer, target.art_memory, 0),
        "bind art parameter memory");
    check(api_.MapMemory(device_, target.art_memory, 0, art_bytes, 0, &target.art_mapped),
        "map art parameters");
}

void Pipeline::ensure_ui_buffer(Target& target, VkDeviceSize required) {
    if (required <= target.ui_capacity)
        return;
    if (required > 16 * 1024 * 1024)
        throw std::runtime_error("editor geometry limit exceeded");
    if (target.ui_mapped) {
        api_.UnmapMemory(device_, target.ui_memory);
        target.ui_mapped = nullptr;
    }
    if (target.ui_buffer) {
        api_.DestroyBuffer(device_, target.ui_buffer, nullptr);
        target.ui_buffer = VK_NULL_HANDLE;
    }
    if (target.ui_memory) {
        api_.FreeMemory(device_, target.ui_memory, nullptr);
        target.ui_memory = VK_NULL_HANDLE;
    }
    target.ui_capacity = std::max(required, target.ui_capacity * 2);
    auto buffer = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    buffer.size = target.ui_capacity;
    buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(api_.CreateBuffer(device_, &buffer, nullptr, &target.ui_buffer),
        "create editor geometry buffer");
    VkMemoryRequirements requirements{};
    api_.GetBufferMemoryRequirements(device_, target.ui_buffer, &requirements);
    resources_.allocate(requirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        target.ui_memory);
    check(api_.BindBufferMemory(device_, target.ui_buffer, target.ui_memory, 0),
        "bind editor geometry memory");
    check(api_.MapMemory(device_, target.ui_memory, 0, target.ui_capacity, 0, &target.ui_mapped),
        "map editor geometry");
}

void Pipeline::upload_ui(Target& target, const ImDrawData& data) {
    const auto vertices = static_cast<VkDeviceSize>(data.TotalVtxCount) * sizeof(ImDrawVert);
    const auto indices = static_cast<VkDeviceSize>(data.TotalIdxCount) * sizeof(ImDrawIdx);
    ensure_ui_buffer(target, vertices + indices);
    auto* v = static_cast<unsigned char*>(target.ui_mapped);
    auto* i = v + vertices;
    for (const auto* list : data.CmdLists) {
        const auto vs = static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const auto is = static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        std::memcpy(v, list->VtxBuffer.Data, vs);
        std::memcpy(i, list->IdxBuffer.Data, is);
        v += vs;
        i += is;
    }
}

void Pipeline::draw_ui(VkCommandBuffer command, const Target& target, const ImDrawData& data) {
    if (!data.TotalVtxCount || !data.TotalIdxCount)
        return;
    const VkDeviceSize offset = 0;
    api_.CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);
    api_.CmdBindVertexBuffers(command, 0, 1, &target.ui_buffer, &offset);
    api_.CmdBindIndexBuffer(command,
        target.ui_buffer,
        static_cast<VkDeviceSize>(data.TotalVtxCount) * sizeof(ImDrawVert),
        sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

    struct Projection {
        float scale[2], translate[2];
        uint32_t srgb;
    } projection{
        {2.0F / static_cast<float>(extent_.width), 2.0F / static_cast<float>(extent_.height)},
        {-1, -1},
        srgb(format_) ? 1U : 0U};

    api_.CmdPushConstants(command,
        layout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(projection),
        &projection);
    uint32_t first_index = 0;
    int32_t vertex_offset = 0;
    for (const auto* list : data.CmdLists) {
        for (const auto& cmd : list->CmdBuffer) {
            if (cmd.UserCallback)
                continue;
            const int x = std::max(0, static_cast<int>(cmd.ClipRect.x));
            const int y = std::max(0, static_cast<int>(cmd.ClipRect.y));
            const int right =
                std::min(static_cast<int>(extent_.width), static_cast<int>(cmd.ClipRect.z));
            const int bottom =
                std::min(static_cast<int>(extent_.height), static_cast<int>(cmd.ClipRect.w));
            if (right <= x || bottom <= y)
                continue;
            const VkRect2D clip{{x, y},
                {static_cast<uint32_t>(right - x), static_cast<uint32_t>(bottom - y)}};
            api_.CmdSetScissor(command, 0, 1, &clip);
            api_.CmdDrawIndexed(command,
                cmd.ElemCount,
                1,
                first_index + cmd.IdxOffset,
                vertex_offset + static_cast<int32_t>(cmd.VtxOffset),
                0);
        }
        first_index += static_cast<uint32_t>(list->IdxBuffer.Size);
        vertex_offset += list->VtxBuffer.Size;
    }
}

void Pipeline::upload(VkCommandBuffer command,
    const Texture& texture,
    VkDeviceSize offset,
    VkExtent2D extent) {
    resources_.barrier(command,
        texture.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.bufferOffset = offset;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {extent.width, extent.height, 1};
    api_.CmdCopyBufferToImage(command,
        staging_,
        texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy);
    resources_.barrier(command,
        texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}
}  // namespace glyphshift::vulkan
