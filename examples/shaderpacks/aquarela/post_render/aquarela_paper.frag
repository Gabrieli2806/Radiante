#version 460
#extension GL_GOOGLE_include_directive : require

// Aquarela, last post pass: the brushed picture (aquarela_wash, from aquarela_brush.frag) laid on paper, written to
// out:ldr before the HUD. In order:
// - colour bleeding: in patches where the paper is taken to be still wet, colour creeps a few pixels across the
//   edges in irregular tongues, and those edges stay soft;
// - edge darkening: where the brushed picture's brightness steps (its own edges, thresholded, so the flat washes the
//   brush left stay clean), pigment pools on the darker side of the step;
// - the paper: a procedural cold-press sheet (rounded tooth, fibres, broad unevenness), fixed to the screen. Its
//   slope nudges where the picture is read (ragged edges at the grain's scale) and catches a light from the top
//   left; its valleys hold more pigment, and in darker washes pigment granulates into them;
// - the palette: less chroma in OKLab, cool shadows and warm lights, lifted blacks, and a warm paper white, so white
//   is the paper and the darkest wash is a cool grey, never black;
// - a gentle glow from Radiante's bloom pyramid, and an optional bare paper border with a ragged edge.
// The density changes use Bousseau et al.'s pigment model ("Interactive watercolor rendering with temporal coherence
// and abstraction", 2006): colour * (1 - (1 - colour) * (density - 1)), which darkens a wash without turning it grey
// and leaves the paper's white alone.
//
// Every setting is live ("runtime": true): the defines read the world uniform, so they are tested with if, not #if.

#include "common/pack_post.glsl"
#include "aquarela/aquarela_noise.glsl"

#ifndef AQUARELA_EDGES
#    define AQUARELA_EDGES 0.5
#endif
#ifndef AQUARELA_BLEED
#    define AQUARELA_BLEED 0.35
#endif
#ifndef AQUARELA_PAPER
#    define AQUARELA_PAPER 0.5
#endif
#ifndef AQUARELA_GRANULATION
#    define AQUARELA_GRANULATION 0.5
#endif
#ifndef AQUARELA_GRAIN_SIZE
#    define AQUARELA_GRAIN_SIZE 1.0
#endif
#ifndef AQUARELA_BORDER
#    define AQUARELA_BORDER 0.2
#endif
#ifndef AQUARELA_PIGMENT
#    define AQUARELA_PIGMENT 0.85
#endif
#ifndef AQUARELA_SATURATION
#    define AQUARELA_SATURATION 0.8
#endif
#ifndef AQUARELA_LIFT
#    define AQUARELA_LIFT 0.12
#endif
#ifndef AQUARELA_WARMTH
#    define AQUARELA_WARMTH 0.5
#endif
#ifndef AQUARELA_COOL_SHADOWS
#    define AQUARELA_COOL_SHADOWS 0.4
#endif
#ifndef AQUARELA_GLOW
#    define AQUARELA_GLOW 0.15
#endif

// The brushed picture, a pack image: set 4 at its sampled_binding. This pass writes out:ldr, so it never samples
// set 3 binding 0.
layout(set = 4, binding = 61) uniform sampler2D aquarelaWash;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

float aquarelaLuma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// The paper's height, 0 (valley) to 1 (the top of a tooth), at a position in paper units (about a pixel at 1080p).
float aquarelaPaperHeight(vec2 p) {
    // Cold-press tooth: rounded bumps about five units across, round points scattered one per cell.
    float tooth = 1.0 - smoothstep(0.0, 0.75, aquarelaCells(p * (1.0 / 5.0), 101u));
    // Fibres and fine unevenness, and the sheet's broad cockling.
    float fine = aquarelaValueNoise(p * (1.0 / 1.7), 103u);
    float broad = aquarelaValueNoise(p * (1.0 / 23.0), 107u);
    return clamp(0.6 * tooth + 0.25 * fine + 0.15 * broad, 0.0, 1.0);
}

// Display colours to OKLab and back (Ottosson 2020), through linear light (Radiante's post passes take display
// colours as about gamma 2.2, as common/pack_post.glsl does).
vec3 aquarelaToOklab(vec3 display) {
    vec3 c = pow(max(display, vec3(0.0)), vec3(2.2));
    vec3 lms = vec3(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b,
                    0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b,
                    0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
    lms = pow(max(lms, vec3(0.0)), vec3(1.0 / 3.0));
    return vec3(0.2104542553 * lms.x + 0.7936177850 * lms.y - 0.0040720468 * lms.z,
                1.9779984951 * lms.x - 2.4285922050 * lms.y + 0.4505937099 * lms.z,
                0.0259040371 * lms.x + 0.7827717662 * lms.y - 0.8086757660 * lms.z);
}

vec3 aquarelaFromOklab(vec3 lab) {
    vec3 lms = vec3(lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z,
                    lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z,
                    lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z);
    lms = lms * lms * lms;
    vec3 c = vec3(4.0767416621 * lms.x - 3.3077115913 * lms.y + 0.2309699292 * lms.z,
                  -1.2684380046 * lms.x + 2.6097574011 * lms.y - 0.3413193965 * lms.z,
                  -0.0041960863 * lms.x - 0.7034186147 * lms.y + 1.7076147010 * lms.z);
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
}

// Bousseau's pigment density: 1 keeps the colour, above 1 a denser, darker wash, below 1 a thinner one.
vec3 aquarelaDensity(vec3 colour, float density) {
    return clamp(colour * (1.0 - (1.0 - colour) * (density - 1.0)), 0.0, 1.0);
}

void main() {
    vec2 size = vec2(textureSize(aquarelaWash, 0));
    vec2 texel = 1.0 / size;
    vec2 uv = fragTexCoord;
    vec2 pixel = uv * size;
    // Sizes are given at 1080p and follow the screen's height, so the painting looks the same at any resolution.
    float scale = size.y / 1080.0;

    float paperAmount = clamp(float(AQUARELA_PAPER), 0.0, 1.0);
    float granulation = clamp(float(AQUARELA_GRANULATION), 0.0, 1.0);
    float bleed = clamp(float(AQUARELA_BLEED), 0.0, 1.0);
    float edges = clamp(float(AQUARELA_EDGES), 0.0, 1.0);
    float border = clamp(float(AQUARELA_BORDER), 0.0, 1.0);

    // The paper and its slope, one pixel each way.
    float grain = scale * clamp(float(AQUARELA_GRAIN_SIZE), 0.25, 4.0);
    vec2 paperPos = pixel / grain;
    float height = aquarelaPaperHeight(paperPos);
    vec2 slope = vec2(aquarelaPaperHeight(paperPos + vec2(1.0 / grain, 0.0)),
                      aquarelaPaperHeight(paperPos + vec2(0.0, 1.0 / grain))) - height;

    // The paint sits on the paper's tooth: read the picture a little off where the paper tilts, so washes end in
    // the ragged edge of the grain rather than on a clean pixel line.
    vec2 at = uv + slope * (paperAmount * 2.5 * scale) * texel;
    vec4 washed = texture(aquarelaWash, at);
    vec3 colour = washed.rgb;

    // ---- Colour bleeding (wet in wet)
    float wet = 0.0;
    if (bleed > 0.0) {
        // Which parts of the sheet were still wet: broad patches covering about a third of the screen (checked on a
        // 1080p screenshot: thresholds of 0.45 to 0.62 wetted three fifths of it).
        wet = smoothstep(0.52, 0.68, aquarelaFbm(pixel / (scale * 260.0), 2, 211u)) * bleed;
        if (wet > 0.001) {
            // The colour creeps in tongues: read from a place pushed about by a noise a dozen pixels across, and
            // softened with four reads round it.
            float reach = 9.0 * scale * bleed;
            vec2 creepAt = pixel / (scale * 11.0);
            vec2 creep = vec2(aquarelaValueNoise(creepAt, 223u), aquarelaValueNoise(creepAt, 227u)) * 2.0 - 1.0;
            vec2 centre = at + creep * reach * texel;
            vec2 apart = 0.5 * reach * texel;
            vec3 spread = 0.4 * texture(aquarelaWash, centre).rgb;
            spread += 0.15 * texture(aquarelaWash, centre + vec2(apart.x, 0.0)).rgb;
            spread += 0.15 * texture(aquarelaWash, centre - vec2(apart.x, 0.0)).rgb;
            spread += 0.15 * texture(aquarelaWash, centre + vec2(0.0, apart.y)).rgb;
            spread += 0.15 * texture(aquarelaWash, centre - vec2(0.0, apart.y)).rgb;
            colour = mix(colour, spread, wet);
        }
    }

    // ---- Edge darkening, from the brushed picture's own brightness steps
    float rim = 0.0;
    if (edges > 0.0) {
        float d = 2.0 * scale;
        float right = aquarelaLuma(texture(aquarelaWash, at + vec2(d, 0.0) * texel).rgb);
        float left = aquarelaLuma(texture(aquarelaWash, at - vec2(d, 0.0) * texel).rgb);
        float below = aquarelaLuma(texture(aquarelaWash, at + vec2(0.0, d) * texel).rgb);
        float above = aquarelaLuma(texture(aquarelaWash, at - vec2(0.0, d) * texel).rgb);
        float here = aquarelaLuma(washed.rgb);
        // The brightness change per d pixels; a step under about 0.03 is the wash's own unevenness and draws nothing.
        float change = 0.5 * length(vec2(right - left, below - above));
        float edge = smoothstep(0.03, 0.10, change);
        // Pigment pools on the darker side of the step, as a wash dries towards its border.
        float darker = smoothstep(0.0, 0.04, 0.25 * (right + left + below + above) - here);
        // Wet edges bleed instead of drying hard: no rim where the colour crept.
        rim = edge * (0.3 + 0.7 * darker) * (1.0 - 0.8 * wet);
    }

    // ---- A gentle glow, added as light before the paint is laid
    float glow = clamp(float(AQUARELA_GLOW), 0.0, 1.0);
    if (glow > 0.0) { colour = radianteAddBloom(colour, uv, glow); }

    // ---- Palette: chroma, cool shadows and warm lights, in OKLab so hues stay put
    vec3 lab = aquarelaToOklab(colour);
    lab.yz *= clamp(float(AQUARELA_SATURATION), 0.0, 2.0);
    float cool = clamp(float(AQUARELA_COOL_SHADOWS), 0.0, 1.0);
    if (cool > 0.0) {
        float shade = smoothstep(0.75, 0.2, lab.x);
        float light = smoothstep(0.7, 0.95, lab.x);
        // Shadows towards ultramarine (blue, a little violet), lights a touch towards yellow ochre.
        lab.y += 0.012 * cool * shade;
        lab.z += cool * (0.015 * light - 0.05 * shade);
    }
    colour = aquarelaFromOklab(lab);

    // ---- The paint's density
    float density = clamp(float(AQUARELA_PIGMENT), 0.25, 2.0);
    // Uneven absorption: the paper takes the paint in broad mottled patches, and its valleys hold more.
    float mottling = aquarelaFbm(pixel / (scale * 70.0), 3, 307u) - 0.5;
    density *= 1.0 + paperAmount * (0.45 * mottling + 0.3 * (0.5 - height));
    // Granulation: in the darker washes pigment settles into the valleys and between the fibres.
    if (granulation > 0.0) {
        float particles = aquarelaValueNoise(pixel / (grain * 1.3), 311u);
        float settle = smoothstep(0.92, 0.25, aquarelaLuma(colour));
        density *= 1.0 + granulation * 0.6 * settle * (0.65 * (0.5 - height) + 0.35 * (particles - 0.5)) * 2.0;
    }
    density *= 1.0 + edges * 1.1 * rim;

    // The bare paper border: the wash stops short of the frame in a ragged line, a little darker where it stopped.
    float painted = 1.0;
    if (border > 0.0) {
        vec2 fromEdge = min(uv, 1.0 - uv) * size / size.y;
        float ragged = (aquarelaFbm(pixel / (scale * 45.0), 3, 401u) - 0.5) * 0.06 * border;
        float width = 0.09 * border;
        painted = smoothstep(0.3 * width, width, min(fromEdge.x, fromEdge.y) + ragged);
        density *= 1.0 + 0.6 * smoothstep(0.2, 0.6, painted) * (1.0 - smoothstep(0.6, 1.0, painted));
    }
    colour = aquarelaDensity(colour, clamp(density, 0.0, 2.0));

    // ---- Lifted blacks: the darkest wash is a cool grey (about Payne's grey), never black.
    vec3 floorColour = clamp(float(AQUARELA_LIFT), 0.0, 0.5) * vec3(0.80, 0.84, 1.0);
    colour = floorColour + (1.0 - floorColour) * colour;

    // ---- The paper: its colour under everything, and its tooth catching a light from the top left (a slope rising
    // to the right or downwards faces that light; y runs down the screen).
    vec3 paper = mix(vec3(1.0), vec3(1.0, 0.955, 0.87), clamp(float(AQUARELA_WARMTH), 0.0, 1.0));
    float tooth = 1.0 + paperAmount * 0.25 * (slope.x + slope.y);
    colour = mix(paper, colour * paper, painted) * tooth;

    outColor = vec4(clamp(colour, 0.0, 1.0), washed.a);
}
