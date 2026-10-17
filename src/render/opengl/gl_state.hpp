#pragma once
#include <array>

#include "gl_api.hpp"

namespace glyphshift::opengl {
class GlState {
 public:
  GlState(const GlApi& api, unsigned clip_count);
  ~GlState();
  GlState(const GlState&) = delete;
  GlState& operator=(const GlState&) = delete;

 private:
  const GlApi& gl_;
  GLint program_{}, vao_{}, active_{}, unpack_buffer_{};
  GLint read_fbo_{}, draw_fbo_{}, default_read_{}, default_draw_{};
  std::array<GLint, 4> viewport_{}, scissor_{};
  std::array<GLboolean, 4> color_{};
  std::array<GLint, 2> polygon_{};
  std::array<GLint, 3> textures_{}, samplers_{};
  inline static constexpr std::array<GLenum, 13> capabilities_{
      GL_BLEND,           GL_DEPTH_TEST,
      GL_STENCIL_TEST,    GL_CULL_FACE,
      GL_SCISSOR_TEST,    GL_FRAMEBUFFER_SRGB,
      GL_DITHER,          GL_RASTERIZER_DISCARD,
      GL_COLOR_LOGIC_OP,  GL_SAMPLE_ALPHA_TO_COVERAGE,
      GL_SAMPLE_COVERAGE, GL_SAMPLE_MASK,
      GL_MULTISAMPLE};
  inline static constexpr std::array<GLenum, 8> pixel_store_{
      GL_UNPACK_ALIGNMENT,   GL_UNPACK_ROW_LENGTH, GL_UNPACK_IMAGE_HEIGHT,
      GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS,  GL_UNPACK_SKIP_IMAGES,
      GL_UNPACK_SWAP_BYTES,  GL_UNPACK_LSB_FIRST};
  std::array<GLboolean, capabilities_.size()> enabled_{};
  std::array<GLint, pixel_store_.size()> unpack_{};
  std::array<GLboolean, 32> clips_{};
  unsigned clip_count_ = 0;
};
}  // namespace glyphshift::opengl
