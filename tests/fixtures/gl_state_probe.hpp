#pragma once
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace fixture {
// Deliberately hostile app-owned state around SwapBuffers. Independent query
// snapshots cover indexed states as well as the default framebuffer routes.
class GlStateProbe {
 public:
  GlStateProbe() : context_(glXGetCurrentContext()) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
      const auto* name = reinterpret_cast<const char*>(
          glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
      if (std::strcmp(name, "GL_ARB_viewport_array") == 0)
        viewport_array_ = true;
      if (std::strcmp(name, "GL_ARB_clip_control") == 0) clip_control_ = true;
    }
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &draw_buffers_);
    glGetIntegerv(GL_MAX_CLIP_DISTANCES, &clips_);
    if (draw_buffers_ > 32 || clips_ > 32)
      throw std::runtime_error("probe limits");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, 4096, nullptr, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenTextures(3, textures_.data());
    glGenSamplers(3, samplers_.data());
    for (unsigned i = 0; i < textures_.size(); ++i) {
      glBindTexture(GL_TEXTURE_2D, textures_[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
      glSamplerParameteri(samplers_[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glSamplerParameteri(samplers_[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           textures_[0], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error("probe framebuffer incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  ~GlStateProbe() {
    if (glXGetCurrentContext() != context_) return;
    glDeleteFramebuffers(1, &framebuffer_);
    glDeleteSamplers(3, samplers_.data());
    glDeleteTextures(3, textures_.data());
    glDeleteBuffers(1, &buffer_);
    glDeleteVertexArrays(1, &vao_);
  }
  void before() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    const std::array<GLenum, 2> routes{GL_FRONT_LEFT, GL_BACK_LEFT};
    glDrawBuffers(2, routes.data());
    glReadBuffer(GL_FRONT);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glUseProgram(0);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer_);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer_);
    for (unsigned i = 0; i < 3; ++i) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, textures_[i]);
      glBindSampler(i, samplers_[i]);
    }
    glActiveTexture(GL_TEXTURE5);
    for (const auto capability : capabilities_) glEnable(capability);
    glEnablei(GL_BLEND, 0);
    glDisablei(GL_BLEND, 1);
    glColorMaski(0, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
    glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_GREATER);
    glCullFace(GL_FRONT_AND_BACK);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glSampleMaski(0, 0);
    for (unsigned i = 0; i < pixel_store_.size(); ++i) {
      const auto target = pixel_store_[i];
      const GLint value = i % 8 == 0 ? 8 : (i % 8 >= 6 ? 1 : 3);
      glPixelStorei(target, value);
    }
    if (viewport_array_) {
      glViewportIndexedf(0, .5F, 1.5F, 63.25F, 47.75F);
      glViewportIndexedf(1, 2.25F, 3.75F, 100.5F, 80.5F);
      glScissorIndexed(0, 2, 3, 5, 7);
      glScissorIndexed(1, 8, 9, 10, 11);
      glEnablei(GL_SCISSOR_TEST, 0);
      glDisablei(GL_SCISSOR_TEST, 1);
    } else {
      glViewport(1, 2, 63, 47);
      glScissor(2, 3, 5, 7);
      glEnable(GL_SCISSOR_TEST);
    }
    if (clip_control_) glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
    glEnable(GL_CLIP_DISTANCE0);
    before_ = snapshot();
  }
  void after(GLuint program, GLuint vao) {
    const auto after = snapshot();
    if (before_ != after) {
      for (std::size_t i = 0; i < before_.size(); ++i)
        if (before_[i] != after[i])
          throw std::runtime_error(
              "GL state changed at snapshot slot " + std::to_string(i) + ": " +
              std::to_string(before_[i]) + " -> " + std::to_string(after[i]));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glDrawBuffer(GL_BACK);
    glUseProgram(program);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    for (const auto capability : capabilities_) glDisable(capability);
    glDisablei(GL_BLEND, 0);
    glDisablei(GL_BLEND, 1);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_DITHER);
    glEnable(GL_MULTISAMPLE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    for (GLint i = 0; i < clips_; ++i)
      glDisable(GL_CLIP_DISTANCE0 + static_cast<GLenum>(i));
    if (viewport_array_)
      glDisablei(GL_SCISSOR_TEST, 0);
    else
      glDisable(GL_SCISSOR_TEST);
    if (clip_control_) glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
    for (unsigned i = 0; i < pixel_store_.size(); ++i)
      glPixelStorei(pixel_store_[i], i % 8 == 0 ? 4 : 0);
  }

 private:
  GLXContext context_;
  GLuint vao_ = 0, buffer_ = 0, framebuffer_ = 0;
  std::array<GLuint, 3> textures_{}, samplers_{};
  bool viewport_array_ = false, clip_control_ = false;
  GLint draw_buffers_ = 0, clips_ = 0;
  std::array<GLint, 512> before_{};
  inline static constexpr std::array<GLenum, 12> capabilities_{
      GL_DEPTH_TEST,      GL_STENCIL_TEST,
      GL_CULL_FACE,       GL_FRAMEBUFFER_SRGB,
      GL_DITHER,          GL_RASTERIZER_DISCARD,
      GL_COLOR_LOGIC_OP,  GL_SAMPLE_ALPHA_TO_COVERAGE,
      GL_SAMPLE_COVERAGE, GL_SAMPLE_MASK,
      GL_MULTISAMPLE,     GL_SCISSOR_TEST};
  inline static constexpr std::array<GLenum, 16> pixel_store_{
      GL_UNPACK_ALIGNMENT,   GL_UNPACK_ROW_LENGTH, GL_UNPACK_IMAGE_HEIGHT,
      GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS,  GL_UNPACK_SKIP_IMAGES,
      GL_UNPACK_SWAP_BYTES,  GL_UNPACK_LSB_FIRST,  GL_PACK_ALIGNMENT,
      GL_PACK_ROW_LENGTH,    GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_PIXELS,
      GL_PACK_SKIP_ROWS,     GL_PACK_SKIP_IMAGES,  GL_PACK_SWAP_BYTES,
      GL_PACK_LSB_FIRST};
  std::array<GLint, 512> snapshot() {
    std::array<GLint, 512> result{};
    std::size_t cursor = 0;
    auto integer = [&](GLenum token, unsigned count = 1) {
      GLint values[16]{};
      glGetIntegerv(token, values);
      for (unsigned i = 0; i < count; ++i) result.at(cursor++) = values[i];
    };
    auto floating = [&](GLenum token, unsigned count = 1) {
      GLfloat values[16]{};
      glGetFloatv(token, values);
      for (unsigned i = 0; i < count; ++i)
        result.at(cursor++) = std::bit_cast<GLint>(values[i]);
    };
    for (const auto token : {GL_CURRENT_PROGRAM,
                             GL_VERTEX_ARRAY_BINDING,
                             GL_ARRAY_BUFFER_BINDING,
                             GL_ELEMENT_ARRAY_BUFFER_BINDING,
                             GL_PIXEL_UNPACK_BUFFER_BINDING,
                             GL_PIXEL_PACK_BUFFER_BINDING,
                             GL_ACTIVE_TEXTURE,
                             GL_READ_FRAMEBUFFER_BINDING,
                             GL_DRAW_FRAMEBUFFER_BINDING,
                             GL_POLYGON_MODE,
                             GL_BLEND_SRC_RGB,
                             GL_BLEND_DST_RGB,
                             GL_BLEND_SRC_ALPHA,
                             GL_BLEND_DST_ALPHA,
                             GL_BLEND_EQUATION_RGB,
                             GL_BLEND_EQUATION_ALPHA,
                             GL_DEPTH_FUNC,
                             GL_DEPTH_WRITEMASK,
                             GL_CULL_FACE_MODE,
                             GL_FRONT_FACE,
                             GL_STENCIL_FUNC,
                             GL_STENCIL_REF,
                             GL_STENCIL_VALUE_MASK,
                             GL_STENCIL_WRITEMASK})
      integer(static_cast<GLenum>(token));
    floating(GL_VIEWPORT, 4);
    integer(GL_SCISSOR_BOX, 4);
    for (const auto capability : capabilities_)
      result.at(cursor++) = glIsEnabled(capability);
    for (GLint i = 0; i < clips_; ++i)
      result.at(cursor++) =
          glIsEnabled(GL_CLIP_DISTANCE0 + static_cast<GLenum>(i));
    for (const auto token : pixel_store_) integer(token);
    for (unsigned i = 0; i < 2; ++i) {
      GLboolean color[4]{};
      glGetBooleani_v(GL_COLOR_WRITEMASK, i, color);
      for (auto value : color) result.at(cursor++) = value;
      result.at(cursor++) = glIsEnabledi(GL_BLEND, i);
    }
    if (viewport_array_) {
      GLfloat viewport[4]{};
      glGetFloati_v(GL_VIEWPORT, 1, viewport);
      for (auto value : viewport)
        result.at(cursor++) = std::bit_cast<GLint>(value);
      GLint scissor[4]{};
      glGetIntegeri_v(GL_SCISSOR_BOX, 1, scissor);
      for (auto value : scissor) result.at(cursor++) = value;
      result.at(cursor++) = glIsEnabledi(GL_SCISSOR_TEST, 1);
    }
    if (clip_control_) {
      integer(GL_CLIP_ORIGIN);
      integer(GL_CLIP_DEPTH_MODE);
    }
    GLint active = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    for (unsigned i = 0; i < 3; ++i) {
      glActiveTexture(GL_TEXTURE0 + i);
      integer(GL_TEXTURE_BINDING_2D);
      GLint sampler = 0;
      glGetIntegeri_v(GL_SAMPLER_BINDING, i, &sampler);
      result.at(cursor++) = sampler;
    }
    glActiveTexture(static_cast<GLenum>(active));
    GLint read = 0, draw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    integer(GL_READ_BUFFER);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    for (GLint i = 0; i < draw_buffers_; ++i)
      integer(GL_DRAW_BUFFER0 + static_cast<GLenum>(i));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
    return result;
  }
};
}  // namespace fixture
