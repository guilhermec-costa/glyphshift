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
layout(set=0,binding=3) uniform sampler2D font_image;
layout(push_constant) uniform Projection { vec2 scale; vec2 translate; uint srgb; } params;
#else
uniform sampler2D font_image;
#endif
LOCATION(0) in vec2 uv;
LOCATION(1) in vec4 tint;
layout(location=0) out vec4 color;
void main() {
    vec3 rgb=tint.rgb;
#ifdef VULKAN
    if(params.srgb!=0u)
        rgb=mix(rgb/12.92,pow((rgb+.055)/1.055,vec3(2.4)),step(vec3(.04045),rgb));
#endif
    color=vec4(rgb,tint.a*texture(font_image,uv).r);
}
