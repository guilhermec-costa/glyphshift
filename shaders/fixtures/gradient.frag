#version 330 core
#ifdef VULKAN
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_enhanced_layouts : enable
layout(push_constant) uniform Parameters {
    vec2 size;
    float time;
    float scroll;
} params;
#else
uniform vec2 size;
uniform float time;
uniform float scroll;
#endif
layout(location = 0) out vec4 color;
void main() {
#ifdef VULKAN
    vec2 uv = gl_FragCoord.xy / params.size;
    float t = params.time;
    float shift = params.scroll;
    vec2 pixel = gl_FragCoord.xy;
#else
    vec2 uv = gl_FragCoord.xy / size;
    uv.y = 1.0 - uv.y;
    float t = time;
    float shift = scroll;
    vec2 pixel = vec2(gl_FragCoord.x, size.y - gl_FragCoord.y);
#endif
    if (shift >= 0.0) {
        // Scrolling mode: 4x4 blocks of integer-hashed gray over the gradient,
        // the content moving left by `shift` pixels; identical on both APIs.
        uvec2 block = uvec2(floor((pixel + vec2(shift, 0.0)) / 4.0));
        uint h = block.x * 73856093u ^ block.y * 19349663u;
        h = (h ^ (h >> 13)) * 1274126177u;
        float noise = float((h >> 8) & 255u) / 255.0;
        color = vec4(vec3(.15 + .7 * noise) * vec3(.6 + .4 * uv.y, .8, 1.0), 1.0);
        return;
    }
    // Top-left origin and direct UNORM color output on both backends.
    float wave = 0.5 + 0.5 * sin(6.2831853 * (uv.x + uv.y - t * 0.2));
    color = vec4(uv.x, uv.y, wave, 1.0);
}
