#version 330 core
// Two passes share this source. With ANALYSIS defined, one fragment per cell
// writes the cell's glyph, palette and contour; otherwise each pixel composes
// glyph masks from that cell data (or runs the classic renderer directly).
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
    vec4 art[5];
} params;
layout(set=0, binding=0) uniform sampler2D source_image;
layout(set=0, binding=1) uniform sampler2D atlas_image;
layout(set=0, binding=2) uniform usampler2D ramp_image;
layout(set=0, binding=4) uniform usampler2D cell_image;
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
uniform usampler2D cell_image;
uniform vec2 size;
uniform vec2 cell;
uniform uint source_color;
uniform uint ramp_count;
uniform float glyph_strength;
uniform vec4 art[5];
#define SIZE size
#define CELL cell
#define SOURCE_COLOR source_color
#define RAMP_COUNT ramp_count
#define OUTPUT_SRGB 0u
#define GLYPH_STRENGTH glyph_strength
#define ART art
#endif
#ifdef ANALYSIS
layout(location=0) out uvec4 cell_data;
#else
layout(location=0) out vec4 color;
#endif
vec3 sample_source(vec2 pixel) {
    ivec2 at = ivec2(clamp(floor(pixel), vec2(0.0), SIZE - vec2(1.0)));
#ifndef VULKAN
    at.y = int(SIZE.y) - 1 - at.y;
#endif
    return texelFetch(source_image, at, 0).rgb;
}
float luma(vec3 rgb) { return dot(rgb, vec3(.2126,.7152,.0722)); }
vec3 saturate_color(vec3 rgb) { return clamp(mix(vec3(luma(rgb)),rgb,ART[4].x),0.0,1.0); }
// Cell palettes travel as 10-bit unorm RGB in one uint: an integer target is
// never blended, whatever blend state the host application left enabled.
uint pack_rgb(vec3 rgb) {
    uvec3 q = uvec3(round(clamp(rgb,0.0,1.0)*1023.0));
    return q.r | (q.g << 10) | (q.b << 20);
}
vec3 unpack_rgb(uint v) {
    return vec3(float(v & 1023u), float((v >> 10) & 1023u), float((v >> 20) & 1023u)) / 1023.0;
}
#ifdef ANALYSIS
// Largest mean squared descriptor error accepted for a shape glyph (mirrors
// glyphshift::shape_fit). Clean outlines fit a character closely; tiled
// texture does not, so it keeps the calm tonal ramp instead of turning into
// scattered punctuation.
const float shape_fit = .06;
// Mean of the 2x2 texels starting at p: a small low-pass that keeps a
// boundary on the cell grid owned by exactly one side.
vec3 box(vec2 p) {
    return (sample_source(p) + sample_source(p + vec2(1,0)) +
            sample_source(p + vec2(0,1)) + sample_source(p + vec2(1,1))) * .25;
}
// Mirrors glyphshift::contour_glyph for one 3x3 neighborhood: -1 or 0..3.
int contour(float v[9]) {
    vec2 gradient = vec2(-v[0]+v[2]-2.0*v[3]+2.0*v[5]-v[6]+v[8],
                         -v[0]-2.0*v[1]-v[2]+v[6]+2.0*v[7]+v[8]) / 4.0;
    float low = v[0], high = v[0];
    for (int i=1;i<9;++i) { low = min(low,v[i]); high = max(high,v[i]); }
    float magnitude = length(gradient);
    if (ART[1].y <= 0.0 || magnitude*ART[1].y <= ART[1].z || magnitude <= .55*(high-low))
        return -1;
    vec2 direction = abs(gradient);
    return direction.x > 2.414214*direction.y ? 0
         : direction.y > 2.414214*direction.x ? 1
         : gradient.x*gradient.y > 0.0 ? 2 : 3;
}
void main() {
    ivec2 index = ivec2(gl_FragCoord.xy);
    vec2 origin = vec2(index) * CELL;
    if (ART[1].x < .5 || any(greaterThanEqual(origin, SIZE))) {
        cell_data = uvec4(0);
        return;
    }
    // Temporal hysteresis: during analysis, cell_image holds this cell's
    // previous decision. The renderer sets ART[4].w to the stability only when
    // that history matches the current grid and glyph set, else 0.
    float stability = ART[4].w;
    uvec4 previous = stability > 0.0 ? texelFetch(cell_image, index, 0) : uvec4(0);
    // z: glyph in bits 0..7, then the tone and internal contrast (8 bits each)
    // at which a tonal glyph was chosen.
    uint previous_glyph = previous.z & 255u, previous_contour = previous.w & 7u;
    float previous_tone = float((previous.z >> 8) & 255u) / 255.0;
    float previous_contrast = float((previous.z >> 16) & 255u) / 255.0;
    bool previous_shape = stability > 0.0 && (previous.w & 8u) != 0u;
    // Sixteen region means cover the whole cell, so thin features and fine
    // texture average out instead of aliasing through a few point samples.
    vec3 regions[16];
    float levels[16];
    vec3 average = vec3(0);
    float dark = 1.0, light = 0.0;
    for (int r=0;r<16;++r) {
        vec2 center = origin + CELL * (vec2(r%4, r/4) + .5) / 4.0;
        regions[r] = box(clamp(floor(center - .5), origin, origin + max(CELL - 2.0, vec2(0))));
        levels[r] = luma(regions[r]);
        average += regions[r] / 16.0;
        dark = min(dark,levels[r]);
        light = max(light,levels[r]);
    }
    // Contours vote on a 5x5 lattice spanning the cell and the first pixels of
    // its right/lower neighbors. Each interior point classifies its own 3x3
    // neighborhood; incoherent texture splits its votes and is rejected.
    float lattice[25];
    for (int j=0;j<5;++j) for (int i=0;i<5;++i)
        lattice[j*5+i] = luma(box(floor(origin + CELL * vec2(i,j) * .25)));
    int votes[4] = int[4](0,0,0,0);
    for (int y=1;y<4;++y) for (int x=1;x<4;++x) {
        float v[9];
        // Doubling quarter-cell differences keeps edge_threshold in the
        // half-cell units of the original 3x3 cell-scale contour test.
        for (int k=0;k<9;++k) v[k] = 2.0 * lattice[(y+k/3-1)*5 + x+k%3-1];
        int axis = contour(v);
        if (axis >= 0) votes[axis] += 1;
    }
    int axis = 0;
    for (int a=1;a<4;++a) if (votes[a] > votes[axis]) axis = a;
    int second = 0;
    for (int a=0;a<4;++a) if (a != axis) second = max(second,votes[a]);
    // A straight contour crossing the cell yields three coherent votes.
    bool edge = votes[axis] >= 3 && votes[axis] >= 2*second;
    if (stability > 0.0 && previous_contour != 0u) {
        // An existing contour persists on weaker evidence than a new one needs,
        // so a stroke near the threshold does not blink frame to frame.
        int kept = int(previous_contour) - 1;
        if (votes[kept] >= 2 && 2*votes[kept] >= votes[axis]) {
            axis = kept;
            edge = true;
        }
    }
    float tone = clamp((luma(average)*ART[0].x-.5)*ART[0].y+.5,0.0,1.0);
    tone = pow(tone,1.0/ART[0].z);
    int ramp_row = ART[2].w >= 0.0 ? 3 : 1;
    uint glyph = texelFetch(ramp_image,ivec2(int(round(tone*255.0)),ramp_row),0).r;
    float decided_tone = tone;
    // Keep the previous tonal glyph while this cell still looks like the cell
    // it was chosen for: tone within +/-0.06 and internal contrast within
    // +/-0.12 at stability 1. Comparing against the decision's own tone, not
    // last frame's, keeps slow drifts from creeping past the band.
    if (stability > 0.0 && !previous_shape &&
        abs(tone - previous_tone) <= .06 * stability &&
        abs((light - dark) - previous_contrast) <= .12 * stability) {
        glyph = previous_glyph;
        decided_tone = previous_tone;
    }
    float peak = max(max(average.r,average.g),average.b);
    // Paper lighter than ink selects dark-ink-on-paper polarity in both color
    // modes: silhouettes then draw the dark group, as tonal ramps already do
    // through glyph_reverse.
    bool invert = luma(ART[2].rgb) < luma(ART[3].rgb);
    vec3 ink = ART[2].rgb, paper = ART[3].rgb;
    if (SOURCE_COLOR != 0u) {
        // Tone controls density. The scene supplies the ink's hue, and the
        // foreground color caps its luminance: bright ink glows on dark paper,
        // dark ink of the same hue prints on light paper. Normalized chroma
        // avoids multiplying source darkness into sparse glyphs a second time.
        vec3 hue = saturate_color(peak > .005 ? average / peak : vec3(1));
        ink = hue * min(1.0, luma(ART[2].rgb) / max(luma(hue), .02));
        ink = mix(ink,min(average*1.4,vec3(1)),ART[3].w);
        paper = saturate_color(mix(ART[3].rgb,average,ART[3].w));
    }
    bool shape = false;
    // A silhouette that was matched last frame needs less contrast to stay.
    float shape_contrast = max(ART[2].w,.0001) * (previous_shape ? 1.0 - .5*stability : 1.0);
    if (ART[2].w >= 0.0 && ART[1].w > .5 && light-dark > shape_contrast) {
        // Split the regions into a light and a dark group, then pick the
        // matchable glyph whose ink layout best fits the light group. Palette
        // entries are constant per cell, never a blend with per-pixel detail.
        float midpoint = (dark+light)*.5;
        vec3 dark_color = vec3(0), light_color = vec3(0);
        float dark_count = 0.0, light_count = 0.0;
        for (int r=0;r<16;++r) {
            if (levels[r] > midpoint) { light_color += regions[r]; light_count += 1.0; }
            else { dark_color += regions[r]; dark_count += 1.0; }
        }
        int matchable = int(texelFetch(ramp_image,ivec2(0,4),0).r) + 1;
        float best = 1e9, kept_error = 1e9;
        uint shaped = 0u;
        for (int g=0;g<matchable;++g) {
            bool was = previous_shape && uint(g) == previous_glyph;
            float error = 0.0;
            // The previous glyph's error is always completed for hysteresis.
            for (int r=0;r<16 && (error<best || was);++r) {
                float target = (levels[r]-dark)/(light-dark);
                float delta = float(texelFetch(ramp_image,ivec2(g,5+r),0).r)/255.0 -
                              (invert ? 1.0-target : target);
                error += delta*delta;
            }
            if (was) kept_error = error;
            if (error < best) { best = error; shaped = uint(g); }
        }
        // Keep last frame's silhouette glyph while it is nearly as good as
        // the new best and still within a widened fit limit.
        float limit = shape_fit * 16.0;
        if (previous_shape && kept_error <= best + .5*stability*limit &&
            kept_error <= limit * (1.0 + stability)) {
            best = kept_error;
            shaped = previous_glyph;
            limit *= 1.0 + stability;
        }
        shape = best <= limit;
        if (shape) glyph = shaped;
        if (shape && SOURCE_COLOR != 0u) {
            // The glyph covers the light group, or the dark group when inverted.
            vec3 light_mean = light_color / light_count, dark_mean = dark_color / dark_count;
            ink = saturate_color(invert ? dark_mean : light_mean);
            paper = saturate_color(mix(ART[3].rgb,invert ? light_mean : dark_mean,ART[3].w));
        }
    }
    // A matched silhouette already owns its boundary; drawing another
    // centered contour over it would replace useful character detail.
    // w: contour axis + 1 (0 none) in bits 0..2, matched silhouette in bit 3.
    uint memory = (uint(round(decided_tone*255.0)) << 8) |
                  (uint(round(clamp(light - dark, 0.0, 1.0)*255.0)) << 16);
    cell_data = uvec4(pack_rgb(ink), pack_rgb(paper), glyph | memory,
                      (edge && !shape ? uint(axis)+1u : 0u) | (shape ? 8u : 0u));
}
#else
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
        ivec2 index = ivec2(floor(pixel / CELL));
        uvec4 data = texelFetch(cell_image, index, 0);
        vec3 ink = unpack_rgb(data.x), paper = unpack_rgb(data.y);
        uint glyph = data.z & 255u;
        if (ART[4].z > .5) {
            // Paper wash: bilinear paper between cell centers, so scene color
            // reads as a soft wash under the ink instead of a tile mosaic.
            vec2 at = pixel / CELL - .5;
            ivec2 base = ivec2(floor(at));
            vec2 f = at - vec2(base);
            ivec2 last = ivec2(ceil(SIZE / CELL)) - 1;
            vec3 p00 = unpack_rgb(texelFetch(cell_image, clamp(base, ivec2(0), last), 0).y);
            vec3 p10 = unpack_rgb(texelFetch(cell_image, clamp(base+ivec2(1,0), ivec2(0), last), 0).y);
            vec3 p01 = unpack_rgb(texelFetch(cell_image, clamp(base+ivec2(0,1), ivec2(0), last), 0).y);
            vec3 p11 = unpack_rgb(texelFetch(cell_image, clamp(base+ivec2(1,1), ivec2(0), last), 0).y);
            paper = mix(mix(p00,p10,f.x),mix(p01,p11,f.x),f.y);
        }
        float fill_mask = ART[1].w > .5 ? glyph_mask(glyph,pixel-origin,true) : 0;
        result = mix(paper,ink,fill_mask);
        uint contour = data.w & 7u;
        if (contour != 0u) {
            float mask = glyph_mask(255u+contour,pixel-origin,true);
            if (ART[0].w > .5) {
                // Choose whichever ink/paper stroke contrasts more with the
                // cell's mean filled color, so contours never punch holes.
                ivec2 corner = ivec2(int(glyph)%16,int(glyph)/16)*ivec2(17,25)+ivec2(16,24);
                float coverage = ART[1].w > .5 ? texelFetch(atlas_image,corner,0).r/(255.0*16*24) : 0;
                float ground = luma(mix(paper,ink,coverage));
                vec3 stroke = abs(luma(ART[2].rgb)-ground) >= abs(luma(ART[3].rgb)-ground)
                            ? ART[2].rgb : ART[3].rgb;
                result = mix(result,stroke,mask);
            } else {
                result = mix(ART[3].rgb,ART[2].rgb,mask);
            }
        }
        if (ART[4].y > 0.0 && ART[1].w > .5) {
            // Glow on dark paper, ink bleed on light paper: pull each pixel
            // toward the ink of nearby glyphs, weighted by their coverage.
            ivec2 last = ivec2(ceil(SIZE / CELL)) - 1;
            vec3 bleed = vec3(0);
            float covered = 0.0, total = 0.0;
            for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
                ivec2 near = clamp(index + ivec2(x,y), ivec2(0), last);
                uvec4 cell_near = texelFetch(cell_image, near, 0);
                int near_glyph = int(cell_near.z & 255u);
                ivec2 corner = ivec2(near_glyph%16,near_glyph/16)*ivec2(17,25)+ivec2(16,24);
                vec2 offset = (pixel - (vec2(near)+.5)*CELL) / CELL;
                float weight = exp(-1.5*dot(offset,offset));
                float coverage = texelFetch(atlas_image,corner,0).r/(255.0*16.0*24.0) * weight;
                bleed += unpack_rgb(cell_near.x) * coverage;
                covered += coverage;
                total += weight;
            }
            if (covered > 0.0)
                result = mix(result, bleed/covered, clamp(2.0*ART[4].y*covered/total,0.0,1.0));
        }
    }
    vec3 scene = sample_source(pixel);
    if (SOURCE_COLOR == 0u) scene = vec3(luma(scene));
    result = mix(scene,result,clamp(GLYPH_STRENGTH,0,1));
    if (OUTPUT_SRGB != 0u)
        result = mix(result/12.92,pow((result+.055)/1.055,vec3(2.4)),step(vec3(.04045),result));
    color = vec4(result,1);
}
#endif
