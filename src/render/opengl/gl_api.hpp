#pragma once
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

#define GLYPHSHIFT_GL_FUNCTIONS(X)                      \
  X(GenBuffers, glGenBuffers)                           \
  X(DeleteBuffers, glDeleteBuffers)                     \
  X(BufferData, glBufferData)                           \
  X(EnableVertexAttribArray, glEnableVertexAttribArray) \
  X(VertexAttribPointer, glVertexAttribPointer)         \
  X(DrawElementsBaseVertex, glDrawElementsBaseVertex)   \
  X(BlendFuncSeparate, glBlendFuncSeparate)             \
  X(BlendEquationSeparate, glBlendEquationSeparate)     \
  X(GetString, glGetString)                             \
  X(GetIntegerv, glGetIntegerv)                         \
  X(GetIntegeri_v, glGetIntegeri_v)                     \
  X(GetBooleanv, glGetBooleanv)                         \
  X(IsEnabled, glIsEnabled)                             \
  X(Enable, glEnable)                                   \
  X(Disable, glDisable)                                 \
  X(ActiveTexture, glActiveTexture)                     \
  X(BindTexture, glBindTexture)                         \
  X(BindSampler, glBindSampler)                         \
  X(GenTextures, glGenTextures)                         \
  X(DeleteTextures, glDeleteTextures)                   \
  X(TexParameteri, glTexParameteri)                     \
  X(TexImage2D, glTexImage2D)                           \
  X(GenFramebuffers, glGenFramebuffers)                 \
  X(DeleteFramebuffers, glDeleteFramebuffers)           \
  X(BindFramebuffer, glBindFramebuffer)                 \
  X(FramebufferTexture2D, glFramebufferTexture2D)       \
  X(CheckFramebufferStatus, glCheckFramebufferStatus)   \
  X(BlitFramebuffer, glBlitFramebuffer)                 \
  X(ReadBuffer, glReadBuffer)                           \
  X(DrawBuffer, glDrawBuffer)                           \
  X(GenVertexArrays, glGenVertexArrays)                 \
  X(BindVertexArray, glBindVertexArray)                 \
  X(DeleteVertexArrays, glDeleteVertexArrays)           \
  X(CreateShader, glCreateShader)                       \
  X(ShaderSource, glShaderSource)                       \
  X(CompileShader, glCompileShader)                     \
  X(GetShaderiv, glGetShaderiv)                         \
  X(GetShaderInfoLog, glGetShaderInfoLog)               \
  X(DeleteShader, glDeleteShader)                       \
  X(CreateProgram, glCreateProgram)                     \
  X(AttachShader, glAttachShader)                       \
  X(LinkProgram, glLinkProgram)                         \
  X(GetProgramiv, glGetProgramiv)                       \
  X(GetProgramInfoLog, glGetProgramInfoLog)             \
  X(UseProgram, glUseProgram)                           \
  X(DeleteProgram, glDeleteProgram)                     \
  X(GetUniformLocation, glGetUniformLocation)           \
  X(Uniform1i, glUniform1i)                             \
  X(Uniform1ui, glUniform1ui)                           \
  X(Uniform1f, glUniform1f)                             \
  X(Uniform4fv, glUniform4fv)                           \
  X(Uniform2f, glUniform2f)                             \
  X(Viewport, glViewport)                               \
  X(Scissor, glScissor)                                 \
  X(ColorMask, glColorMask)                             \
  X(PolygonMode, glPolygonMode)                         \
  X(PixelStorei, glPixelStorei)                         \
  X(BindBuffer, glBindBuffer)                           \
  X(DrawArrays, glDrawArrays)                           \
  X(GetQueryiv, glGetQueryiv)                           \
  X(GetStringi, glGetStringi)                           \
  X(GetFloatv, glGetFloatv)                             \
  X(GetBooleani_v, glGetBooleani_v)                     \
  X(IsEnabledi, glIsEnabledi)                           \
  X(Enablei, glEnablei)                                 \
  X(Disablei, glDisablei)                               \
  X(ColorMaski, glColorMaski)                           \
  X(DrawBuffers, glDrawBuffers)                         \
  X(GetTexLevelParameteriv, glGetTexLevelParameteriv)
namespace glyphshift::opengl {
struct GlApi {
#define MEMBER(member, name) decltype(&::name) member = nullptr;
  GLYPHSHIFT_GL_FUNCTIONS(MEMBER)
#undef MEMBER
  decltype(&::glViewportIndexedf) ViewportIndexedf = nullptr;
  decltype(&::glScissorIndexed) ScissorIndexed = nullptr;
  decltype(&::glClipControl) ClipControl = nullptr;
  decltype(&::glBlendFuncSeparatei) BlendFuncSeparatei = nullptr;
  decltype(&::glBlendEquationSeparatei) BlendEquationSeparatei = nullptr;
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
