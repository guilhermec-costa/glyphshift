#version 330 core
#ifdef VULKAN
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_enhanced_layouts : enable
layout(push_constant) uniform Parameters {
    vec2 size;
    float time;
} params;
#else
uniform vec2 size;
uniform float time;
#endif
layout(location = 0) out vec4 color;
void main() {
#ifdef VULKAN
    vec2 uv = gl_FragCoord.xy / params.size;
    float t = params.time;
#else
    vec2 uv = gl_FragCoord.xy / size;
    uv.y = 1.0 - uv.y;
    float t = time;
#endif
    // Top-left origin and direct UNORM color output on both backends.
    float wave = 0.5 + 0.5 * sin(6.2831853 * (uv.x + uv.y - t * 0.2));
    color = vec4(uv.x, uv.y, wave, 1.0);
}
