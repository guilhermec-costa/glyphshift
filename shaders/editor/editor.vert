#version 330 core
#ifdef VULKAN
#define LOCATION(x) layout(location=x)
#else
#define LOCATION(x)
#endif
#ifdef VULKAN
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_enhanced_layouts : enable
layout(push_constant) uniform Projection { vec2 scale; vec2 translate; uint srgb; } params;
#define SCALE params.scale
#define TRANSLATE params.translate
#else
uniform vec2 scale;
uniform vec2 translate;
#define SCALE scale
#define TRANSLATE translate
#endif
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 vertex_color;
LOCATION(0) out vec2 uv;
LOCATION(1) out vec4 tint;
void main() {
    uv=texcoord; tint=vertex_color;
    gl_Position=vec4(position*SCALE+TRANSLATE,0,1);
}
