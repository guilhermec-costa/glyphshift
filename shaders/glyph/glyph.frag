#version 330 core
#ifdef VULKAN
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_enhanced_layouts : enable
layout(push_constant) uniform Parameters {
    vec2 size;
    vec2 cell;
    uint source_color;
    uint ramp_count;
    uint output_srgb;
    float glyph_strength;
} params;
layout(set=0, binding=0) uniform sampler2D source_image;
layout(set=0, binding=1) uniform sampler2D atlas_image;
layout(set=0, binding=2) uniform usampler2D ramp_image;
#define SIZE params.size
#define CELL params.cell
#define SOURCE_COLOR params.source_color
#define RAMP_COUNT params.ramp_count
#define OUTPUT_SRGB params.output_srgb
#define GLYPH_STRENGTH params.glyph_strength
#else
uniform sampler2D source_image;
uniform sampler2D atlas_image;
uniform usampler2D ramp_image;
uniform vec2 size;
uniform vec2 cell;
uniform uint source_color;
uniform uint ramp_count;
uniform float glyph_strength;
#define SIZE size
#define CELL cell
#define SOURCE_COLOR source_color
#define RAMP_COUNT ramp_count
#define OUTPUT_SRGB 0u
#define GLYPH_STRENGTH glyph_strength
#endif
layout(location=0) out vec4 color;
vec3 sample_source(vec2 pixel) {
    ivec2 at = ivec2(clamp(floor(pixel), vec2(0.0), SIZE - vec2(1.0)));
#ifndef VULKAN
    at.y = int(SIZE.y) - 1 - at.y;
#endif
    return texelFetch(source_image, at, 0).rgb;
}
void main() {
    vec2 pixel = gl_FragCoord.xy;
#ifndef VULKAN
    pixel.y = SIZE.y - pixel.y;
#endif
    vec2 origin = floor(pixel / CELL) * CELL;
    vec3 average = (sample_source(origin + CELL * vec2(.25,.25)) +
                    sample_source(origin + CELL * vec2(.75,.25)) +
                    sample_source(origin + CELL * vec2(.25,.75)) +
                    sample_source(origin + CELL * vec2(.75,.75))) * .25;
    float luminance = dot(average, vec3(.2126,.7152,.0722));
    uint index = min(uint(clamp(luminance, 0.0, 1.0) * float(RAMP_COUNT)), RAMP_COUNT - 1u);
    uint glyph = texelFetch(ramp_image, ivec2(int(index),0), 0).r;
    ivec2 tile = textureSize(atlas_image, 0) / 16;
    ivec2 local = ivec2(floor(fract(pixel / CELL) * vec2(tile)));
    ivec2 atlas_at = ivec2(int(glyph) % 16, int(glyph) / 16) * tile + local;
    float mask = texelFetch(atlas_image, atlas_at, 0).r;
    vec3 result = (SOURCE_COLOR != 0u ? average : vec3(1.0)) * mask;
    // Preserve fine silhouettes and HUD edges when the user prefers a readable
    // scene; strength 1 keeps the pure glyph mode, including binary mono.
    vec3 scene = sample_source(pixel);
    if (SOURCE_COLOR == 0u)
        scene = vec3(dot(scene, vec3(.2126,.7152,.0722)));
    result = mix(scene, result, clamp(GLYPH_STRENGTH, 0.0, 1.0));
    if (OUTPUT_SRGB != 0u)
        result = mix(result / 12.92, pow((result + .055) / 1.055, vec3(2.4)), step(vec3(.04045), result));
    color = vec4(result, 1.0);
}
