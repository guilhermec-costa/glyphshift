#pragma once
#include <array>

#include "gl_api.hpp"

namespace glyphshift::opengl {
class GlState {
public:
    GlState(const GlApi& api,
        unsigned clip_count,
        unsigned draw_buffers,
        bool viewport_array,
        bool clip_control,
        bool indexed_blend);
    ~GlState();
    GlState(const GlState&) = delete;
    GlState& operator=(const GlState&) = delete;

private:
    const GlApi& gl_;
    bool indexed_blend_ = false;
    GLint array_buffer_ = 0;
    std::array<std::array<GLint, 6>, 32> blend_parameters_{};
    inline static constexpr std::array<GLenum, 6> blend_names_{GL_BLEND_SRC_RGB,
        GL_BLEND_DST_RGB,
        GL_BLEND_SRC_ALPHA,
        GL_BLEND_DST_ALPHA,
        GL_BLEND_EQUATION_RGB,
        GL_BLEND_EQUATION_ALPHA};
    GLint program_{}, vao_{}, active_{}, unpack_buffer_{};
    GLint read_fbo_{}, draw_fbo_{}, default_read_{};
    std::array<GLenum, 32> default_draws_{};
    unsigned draw_buffers_ = 0;
    bool viewport_array_ = false, clip_control_ = false;
    GLboolean blend_{}, scissor_enabled_{};
    GLint clip_origin_{}, clip_depth_{};
    std::array<GLfloat, 4> viewport_{};
    std::array<GLint, 4> scissor_{};
    std::array<GLboolean, 4> color_{};
    std::array<GLint, 2> polygon_{};
    // Units 0..7: source, atlas, ramp, cell grid, exposure meter, and the
    // anchored grid's luminance, motion costs and camera state.
    std::array<GLint, 8> textures_{}, samplers_{};
    inline static constexpr std::array<GLenum, 11> capabilities_{GL_DEPTH_TEST,
        GL_STENCIL_TEST,
        GL_CULL_FACE,
        GL_FRAMEBUFFER_SRGB,
        GL_DITHER,
        GL_RASTERIZER_DISCARD,
        GL_COLOR_LOGIC_OP,
        GL_SAMPLE_ALPHA_TO_COVERAGE,
        GL_SAMPLE_COVERAGE,
        GL_SAMPLE_MASK,
        GL_MULTISAMPLE};
    inline static constexpr std::array<GLenum, 8> pixel_store_{GL_UNPACK_ALIGNMENT,
        GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_IMAGE_HEIGHT,
        GL_UNPACK_SKIP_PIXELS,
        GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_IMAGES,
        GL_UNPACK_SWAP_BYTES,
        GL_UNPACK_LSB_FIRST};
    std::array<GLboolean, capabilities_.size()> enabled_{};
    std::array<GLint, pixel_store_.size()> unpack_{};
    std::array<GLboolean, 32> clips_{};
    unsigned clip_count_ = 0;
};
}  // namespace glyphshift::opengl
