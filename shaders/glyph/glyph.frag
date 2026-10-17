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
    vec4 art[4];
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
#define ART params.art
#else
uniform sampler2D source_image;
uniform sampler2D atlas_image;
uniform usampler2D ramp_image;
uniform vec2 size;
uniform vec2 cell;
uniform uint source_color;
uniform uint ramp_count;
uniform float glyph_strength;
uniform vec4 art[4];
#define SIZE size
#define CELL cell
#define SOURCE_COLOR source_color
#define RAMP_COUNT ramp_count
#define OUTPUT_SRGB 0u
#define GLYPH_STRENGTH glyph_strength
#define ART art
#endif
layout(location=0) out vec4 color;
vec3 sample_source(vec2 pixel) {
    ivec2 at = ivec2(clamp(floor(pixel), vec2(0.0), SIZE - vec2(1.0)));
#ifndef VULKAN
    at.y = int(SIZE.y) - 1 - at.y;
#endif
    return texelFetch(source_image, at, 0).rgb;
}
// Each glyph owns a 17x25 summed-area tile around its 16x24 coverage mask.
// Integer prefixes remain exact in R32F; interpolation integrates fractional
// texel footprints without mip bleeding between neighboring glyphs.
float prefix(ivec2 base, vec2 p) {
    p = clamp(p, vec2(0), vec2(16,24));
    ivec2 a = ivec2(floor(p));
    ivec2 b = min(a + 1, ivec2(16,24));
    vec2 f = fract(p);
    return mix(mix(texelFetch(atlas_image,base+a,0).r,
                   texelFetch(atlas_image,base+ivec2(b.x,a.y),0).r,f.x),
               mix(texelFetch(atlas_image,base+ivec2(a.x,b.y),0).r,
                   texelFetch(atlas_image,base+b,0).r,f.x),f.y);
}
float glyph_mask(uint glyph, vec2 local, bool filtered) {
    ivec2 base = ivec2(int(glyph)%16,int(glyph)/16) * ivec2(17,25);
    vec2 lo, hi;
    if (filtered) {
        lo = clamp((local - vec2(.5)) / CELL * vec2(16,24),vec2(0),vec2(16,24));
        hi = clamp((local + vec2(.5)) / CELL * vec2(16,24),vec2(0),vec2(16,24));
    } else {
        lo = floor(local / CELL * vec2(16,24));
        hi = lo + 1;
    }
    float sum = prefix(base,hi)-prefix(base,vec2(lo.x,hi.y))
              -prefix(base,vec2(hi.x,lo.y))+prefix(base,lo);
    return clamp(sum / (255.0 * max((hi.x-lo.x)*(hi.y-lo.y),.00001)),0,1);
}
float luma(vec3 rgb) { return dot(rgb, vec3(.2126,.7152,.0722)); }
void main() {
    vec2 pixel = gl_FragCoord.xy;
#ifndef VULKAN
    pixel.y = SIZE.y - pixel.y;
#endif
    vec2 origin = floor(pixel / CELL) * CELL;
    vec3 result;
    if (ART[1].x < .5) {
        vec3 average = (sample_source(origin + CELL * vec2(.25,.25)) +
                        sample_source(origin + CELL * vec2(.75,.25)) +
                        sample_source(origin + CELL * vec2(.25,.75)) +
                        sample_source(origin + CELL * vec2(.75,.75))) * .25;
        uint index = min(uint(clamp(luma(average),0,1)*float(RAMP_COUNT)),RAMP_COUNT-1u);
        uint glyph = texelFetch(ramp_image,ivec2(int(index),0),0).r;
        result = (SOURCE_COLOR != 0u ? average : vec3(1)) * glyph_mask(glyph,pixel-origin,false);
    } else {
        // Cell-scale low-pass samples suppress small texture details before
        // direction classification; the shader never needs scene depth.
        vec3 samples[9];
        float levels[9];
        vec3 average = vec3(0);
        float low = 1, high = 0;
        for (int y=0;y<3;++y) for (int x=0;x<3;++x) {
            int i = y*3+x;
            samples[i] = sample_source(origin + CELL * vec2(x,y)*.5);
            levels[i] = luma(samples[i]);
            float weight = float((x == 1 ? 2 : 1) * (y == 1 ? 2 : 1));
            average += samples[i] * weight / 16.0;
            low = min(low,levels[i]); high = max(high,levels[i]);
        }
        vec2 gradient = vec2(-levels[0]+levels[2]-2*levels[3]+2*levels[5]-levels[6]+levels[8],
                             -levels[0]-2*levels[1]-levels[2]+levels[6]+2*levels[7]+levels[8]) / 4.0;
        float magnitude = length(gradient);
        bool edge = ART[1].y > 0 && magnitude*ART[1].y > ART[1].z &&
                    magnitude > .55 * (high-low);
        float tone = clamp((luma(average)*ART[0].x-.5)*ART[0].y+.5,0,1);
        tone = pow(tone,1.0/ART[0].z);
        uint glyph = texelFetch(ramp_image,ivec2(int(round(tone*255)),1),0).r;
        float fill_mask = ART[1].w > .5 ? glyph_mask(glyph,pixel-origin,true) : 0;
        float peak = max(max(average.r,average.g),average.b);
        // Tone controls density; normalized chroma supplies ink color without
        // multiplying the source darkness into sparse glyphs a second time.
        vec3 ink = (SOURCE_COLOR != 0u && peak > .005) ? average / peak : ART[2].rgb;
        result = mix(ART[3].rgb,ink,fill_mask);
        if (edge) {
            vec2 direction = abs(gradient);
            uint axis = direction.x > 2.414214*direction.y ? 0u
                      : direction.y > 2.414214*direction.x ? 1u
                      : gradient.x*gradient.y > 0 ? 2u : 3u;
            float mask = glyph_mask(256u+axis,pixel-origin,true);
            if (ART[0].w > .5) {
                ivec2 corner = ivec2(int(glyph)%16,int(glyph)/16)*ivec2(17,25)+ivec2(16,24);
                float coverage = ART[1].w > .5 ? texelFetch(atlas_image,corner,0).r/(255.0*16*24) : 0;
                float ground = luma(mix(ART[3].rgb,ink,coverage));
                vec3 stroke = abs(luma(ART[2].rgb)-ground) >= abs(luma(ART[3].rgb)-ground)
                            ? ART[2].rgb : ART[3].rgb;
                result = mix(result,stroke,mask);
            } else {
                result = mix(ART[3].rgb,ART[2].rgb,mask);
            }
        }
    }
    vec3 scene = sample_source(pixel);
    if (SOURCE_COLOR == 0u) scene = vec3(luma(scene));
    result = mix(scene,result,clamp(GLYPH_STRENGTH,0,1));
    if (OUTPUT_SRGB != 0u)
        result = mix(result/12.92,pow((result+.055)/1.055,vec3(2.4)),step(vec3(.04045),result));
    color = vec4(result,1);
}
