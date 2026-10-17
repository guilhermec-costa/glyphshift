#pragma once
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

#define GLYPHSHIFT_GL_FUNCTIONS(X)                \
  X(GetString, glGetString)                       \
  X(GetIntegerv, glGetIntegerv)                   \
  X(GetIntegeri_v, glGetIntegeri_v)               \
  X(GetBooleanv, glGetBooleanv)                   \
  X(IsEnabled, glIsEnabled)                       \
  X(Enable, glEnable)                             \
  X(Disable, glDisable)                           \
  X(BindTexture, glBindTexture)                   \
  X(BindSampler, glBindSampler)                   \
  X(GenTextures, glGenTextures)                   \
  X(TexParameteri, glTexParameteri)               \
  X(TexImage2D, glTexImage2D)                     \
  X(GenFramebuffers, glGenFramebuffers)           \
  X(FramebufferTexture2D, glFramebufferTexture2D) \
  X(ReadBuffer, glReadBuffer)                     \
  X(DrawBuffer, glDrawBuffer)                     \
  X(DeleteVertexArrays, glDeleteVertexArrays)     \
  X(GetShaderiv, glGetShaderiv)                   \
  X(CreateProgram, glCreateProgram)               \
  X(GetProgramiv, glGetProgramiv)                 \
  X(DeleteProgram, glDeleteProgram)               \
  X(Uniform1ui, glUniform1ui)                     \
  X(Scissor, glScissor)
namespace glyphshift::opengl {
struct GlApi {
#define MEMBER(member, name) decltype(&::name) member = nullptr;
  GLYPHSHIFT_GL_FUNCTIONS(MEMBER)
#undef MEMBER
  bool available = false;
};
const GlApi& api();
struct GlxApi {
  decltype(&glXGetCurrentContext) context = nullptr;
  decltype(&glXGetCurrentDrawable) drawable = nullptr;
  decltype(&glXGetCurrentDisplay) display = nullptr;
  decltype(&glXQueryDrawable) query = nullptr;
};
const GlxApi& glx();
}  // namespace glyphshift::opengl
