#include "gl_state.hpp"
namespace glyphshift::opengl {
GlState::GlState(const GlApi& gl, unsigned clip_count)
    : gl_(gl), clip_count_(clip_count) {
  gl.GetIntegerv(GL_CURRENT_PROGRAM, &program_);
  gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_);
  gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active_);
  gl.GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer_);
  gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo_);
  gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo_);
  gl.GetIntegerv(GL_VIEWPORT, viewport_.data());
  gl.GetIntegerv(GL_SCISSOR_BOX, scissor_.data());
  gl.GetBooleanv(GL_COLOR_WRITEMASK, color_.data());
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
  gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  gl.GetIntegerv(GL_READ_BUFFER, &default_read_);
  gl.ReadBuffer(GL_BACK);
  gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  gl.GetIntegerv(GL_DRAW_BUFFER, &default_draw_);
  gl.DrawBuffer(GL_BACK);
}
GlState::~GlState() {
  const auto& gl = gl_;
  gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  gl.ReadBuffer(static_cast<GLenum>(default_read_));
  gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  gl.DrawBuffer(static_cast<GLenum>(default_draw_));
  gl.BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_fbo_));
  gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_fbo_));
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
  gl.Viewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
  gl.Scissor(scissor_[0], scissor_[1], scissor_[2], scissor_[3]);
  gl.ColorMask(color_[0], color_[1], color_[2], color_[3]);
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
