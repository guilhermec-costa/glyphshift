#include "gl_state.hpp"
namespace glyphshift::opengl {
GlState::GlState(const GlApi& gl, unsigned clip_count, unsigned draw_buffers,
                 bool viewport_array, bool clip_control, bool indexed_blend)
    : gl_(gl),
      draw_buffers_(draw_buffers),
      viewport_array_(viewport_array),
      clip_control_(clip_control),
      clip_count_(clip_count) {
  indexed_blend_ = indexed_blend;
  gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer_);
  for (unsigned i = 0; i < (indexed_blend_ ? draw_buffers_ : 1); ++i)
    for (unsigned j = 0; j < blend_names_.size(); ++j) {
      if (indexed_blend_)
        gl.GetIntegeri_v(blend_names_[j], i, &blend_parameters_[i][j]);
      else
        gl.GetIntegerv(blend_names_[j], &blend_parameters_[i][j]);
    }
  gl.GetIntegerv(GL_CURRENT_PROGRAM, &program_);
  gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_);
  gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active_);
  gl.GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer_);
  gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo_);
  gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo_);
  gl.GetFloatv(GL_VIEWPORT, viewport_.data());
  gl.GetIntegerv(GL_SCISSOR_BOX, scissor_.data());
  gl.GetBooleani_v(GL_COLOR_WRITEMASK, 0, color_.data());
  blend_ = gl.IsEnabledi(GL_BLEND, 0);
  gl.Disablei(GL_BLEND, 0);
  scissor_enabled_ = viewport_array_ ? gl.IsEnabledi(GL_SCISSOR_TEST, 0)
                                     : gl.IsEnabled(GL_SCISSOR_TEST);
  if (viewport_array_)
    gl.Disablei(GL_SCISSOR_TEST, 0);
  else
    gl.Disable(GL_SCISSOR_TEST);
  if (clip_control_) {
    gl.GetIntegerv(GL_CLIP_ORIGIN, &clip_origin_);
    gl.GetIntegerv(GL_CLIP_DEPTH_MODE, &clip_depth_);
    gl.ClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
  }
  gl.GetIntegerv(GL_POLYGON_MODE, polygon_.data());
  GLint profile = 0;
  gl.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
  if (profile & GL_CONTEXT_CORE_PROFILE_BIT) polygon_[1] = polygon_[0];
  for (unsigned i = 0; i < textures_.size(); ++i) {
    gl.ActiveTexture(GL_TEXTURE0 + i);
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &textures_[i]);
    gl.GetIntegeri_v(GL_SAMPLER_BINDING, i, &samplers_[i]);
  }
  for (unsigned i = 0; i < enabled_.size(); ++i) {
    enabled_[i] = gl.IsEnabled(capabilities_[i]);
    gl.Disable(capabilities_[i]);
  }
  for (unsigned i = 0; i < clip_count_; ++i) {
    clips_[i] = gl.IsEnabled(GL_CLIP_DISTANCE0 + i);
    gl.Disable(GL_CLIP_DISTANCE0 + i);
  }
  for (unsigned i = 0; i < unpack_.size(); ++i) {
    gl.GetIntegerv(pixel_store_[i], &unpack_[i]);
    gl.PixelStorei(pixel_store_[i], i == 0 ? 1 : 0);
  }
  gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  gl.ColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  gl.GetIntegerv(GL_READ_BUFFER, &default_read_);
  gl.ReadBuffer(GL_BACK);
  gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  for (unsigned i = 0; i < draw_buffers_; ++i) {
    GLint value = 0;
    gl.GetIntegerv(GL_DRAW_BUFFER0 + i, &value);
    default_draws_[i] = static_cast<GLenum>(value);
  }
  gl.DrawBuffer(GL_BACK);
}
GlState::~GlState() {
  const auto& gl = gl_;
  gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  gl.ReadBuffer(static_cast<GLenum>(default_read_));
  gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  unsigned count = draw_buffers_;
  while (count > 1 && default_draws_[count - 1] == GL_NONE) --count;
  if (count == 1)
    gl.DrawBuffer(default_draws_[0]);
  else
    gl.DrawBuffers(static_cast<GLsizei>(count), default_draws_.data());
  gl.BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_fbo_));
  gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_fbo_));
  gl.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array_buffer_));
  for (unsigned i = 0; i < (indexed_blend_ ? draw_buffers_ : 1); ++i) {
    const auto& p = blend_parameters_[i];
    if (indexed_blend_) {
      gl.BlendFuncSeparatei(
          i, static_cast<GLenum>(p[0]), static_cast<GLenum>(p[1]),
          static_cast<GLenum>(p[2]), static_cast<GLenum>(p[3]));
      gl.BlendEquationSeparatei(i, static_cast<GLenum>(p[4]),
                                static_cast<GLenum>(p[5]));
    } else {
      gl.BlendFuncSeparate(static_cast<GLenum>(p[0]), static_cast<GLenum>(p[1]),
                           static_cast<GLenum>(p[2]),
                           static_cast<GLenum>(p[3]));
      gl.BlendEquationSeparate(static_cast<GLenum>(p[4]),
                               static_cast<GLenum>(p[5]));
    }
  }
  gl.UseProgram(static_cast<GLuint>(program_));
  gl.BindVertexArray(static_cast<GLuint>(vao_));
  for (unsigned i = 0; i < textures_.size(); ++i) {
    gl.ActiveTexture(GL_TEXTURE0 + i);
    gl.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textures_[i]));
    gl.BindSampler(i, static_cast<GLuint>(samplers_[i]));
  }
  gl.ActiveTexture(static_cast<GLenum>(active_));
  gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack_buffer_));
  for (unsigned i = 0; i < unpack_.size(); ++i)
    gl.PixelStorei(pixel_store_[i], unpack_[i]);
  if (viewport_array_) {
    gl.ViewportIndexedf(0, viewport_[0], viewport_[1], viewport_[2],
                        viewport_[3]);
    gl.ScissorIndexed(0, scissor_[0], scissor_[1], scissor_[2], scissor_[3]);
    (scissor_enabled_ ? gl.Enablei : gl.Disablei)(GL_SCISSOR_TEST, 0);
  } else {
    gl.Viewport(
        static_cast<GLint>(viewport_[0]), static_cast<GLint>(viewport_[1]),
        static_cast<GLsizei>(viewport_[2]), static_cast<GLsizei>(viewport_[3]));
    gl.Scissor(scissor_[0], scissor_[1], scissor_[2], scissor_[3]);
    (scissor_enabled_ ? gl.Enable : gl.Disable)(GL_SCISSOR_TEST);
  }
  if (clip_control_)
    gl.ClipControl(static_cast<GLenum>(clip_origin_),
                   static_cast<GLenum>(clip_depth_));
  gl.ColorMaski(0, color_[0], color_[1], color_[2], color_[3]);
  (blend_ ? gl.Enablei : gl.Disablei)(GL_BLEND, 0);
  if (polygon_[0] == polygon_[1])
    gl.PolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygon_[0]));
  else {
    gl.PolygonMode(GL_FRONT, static_cast<GLenum>(polygon_[0]));
    gl.PolygonMode(GL_BACK, static_cast<GLenum>(polygon_[1]));
  }
  for (unsigned i = 0; i < enabled_.size(); ++i)
    (enabled_[i] ? gl.Enable : gl.Disable)(capabilities_[i]);
  for (unsigned i = 0; i < clip_count_; ++i)
    (clips_[i] ? gl.Enable : gl.Disable)(GL_CLIP_DISTANCE0 + i);
}
}  // namespace glyphshift::opengl
