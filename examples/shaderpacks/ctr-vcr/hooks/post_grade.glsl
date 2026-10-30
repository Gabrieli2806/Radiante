#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// CTR VCR (Radiante port): an old CRT showing a VHS tape, over Radiante's path-traced picture. Ported from the Iris
// pack CTR VCR by millennIumAMbiguity (Apache-2.0, LICENSE-CTRVCR): shaders/program/composite.fsh (screen curve,
// screen tear, jittered bloom, the tape's colour blur - red pulled from the left, blue from the right -, tint, grain,
// static and its tearing row, saturation, colour depth and dithering, dark edges) and composite1.fsh (scanlines),
// with Random_float and hash from shaders/common/hash.glsl. Every setting is live.
//
// What changed for Radiante: CTR VCR runs these as two passes over the scene picture; here they are one pass over the
// finished, tone mapped picture (hookPostGradeInput), with the scanlines last. Its scanlines alternate every screen
// pixel, which at 1440p and 4K is too fine to read as a CRT: CTRVCR_SCANLINE_SIZE 0 (the default) keeps about 540
// lines whatever the resolution, as a TV's picture had. Its clock is frameTimeCounter; Radiante's world clock
// (worldUBO.gameTime, a day's fraction) in seconds stands in for it.

#ifndef CTRVCR_DISTORTION
#    define CTRVCR_DISTORTION 0.05
#endif
#ifndef CTRVCR_DARK_EDGES
#    define CTRVCR_DARK_EDGES 0.0
#endif
#ifndef CTRVCR_SCANLINES
#    define CTRVCR_SCANLINES 0.5
#endif
#ifndef CTRVCR_SCANLINE_SIZE
#    define CTRVCR_SCANLINE_SIZE 0
#endif
#ifndef CTRVCR_BLUR
#    define CTRVCR_BLUR 0.01
#endif
#ifndef CTRVCR_BLOOM
#    define CTRVCR_BLOOM 0.009
#endif
#ifndef CTRVCR_GRAIN
#    define CTRVCR_GRAIN 0.3
#endif
#ifndef CTRVCR_STATIC
#    define CTRVCR_STATIC 0.001
#endif
#ifndef CTRVCR_TEAR
#    define CTRVCR_TEAR 0.08
#endif
#ifndef CTRVCR_TINT_GREEN
#    define CTRVCR_TINT_GREEN 1.1
#endif
#ifndef CTRVCR_SATURATION
#    define CTRVCR_SATURATION 1.0
#endif
#ifndef CTRVCR_COLOR_LEVELS
#    define CTRVCR_COLOR_LEVELS 0
#endif
#ifndef CTRVCR_BLACK_WHITE
#    define CTRVCR_BLACK_WHITE 0.0
#endif

// shaders/common/hash.glsl
float ctrRandom(vec2 seed) {
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453123);
}

float ctrHash(vec2 p, float time) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y + time);
}

vec3 ctrSample(vec2 uv) {
    return texture(hookPostGradeInput, uv).rgb;
}

// COLOR_DITERING 2: the 4x4 Bayer threshold at the screen pixel.
float ctrDither(vec2 pixel) {
    const float bayer[16] = float[](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0,
                                    5.0);
    ivec2 ip = ivec2(mod(pixel, 4.0));
    return bayer[ip.x + ip.y * 4] / 16.0;
}

void hookPostGrade(inout vec3 color, vec2 uvIn) {
    vec2 size = vec2(textureSize(hookPostGradeInput, 0));
    float seconds = worldUBO.gameTime * 1200.0;
    float time = mod(seconds, 8192.0);
    float time8 = mod(seconds * 7.987, 8192.0);

    // The curved glass: farther from the centre, further out.
    float distortionFactor = float(CTRVCR_DISTORTION);
    vec2 dir = uvIn - 0.5;
    float dist = length(dir);
    vec2 texcoord = 0.5 + dir * (1.0 + distortionFactor * dist * dist) * (1.0 - distortionFactor / 3.0);
    vec2 texcoordScaled = texcoord;

    float uvy = texcoordScaled.y;
    float uvx = texcoordScaled.x;
    float v = ctrHash(uvIn, time8);
    float v2 = ctrHash(uvIn, time8 + 456.789);
    float vRow = ctrRandom(vec2(time, floor(uvy)));

    // Screen tear: a band that rolls up the picture, showing the line where it starts.
    float tearSize = float(CTRVCR_TEAR);
    if (tearSize > 0.0) {
        const float TEAR_DELAY = 100.0;
        const float TEAR_SPEED = 1.3;
        float y1 = mod(TEAR_SPEED * time + tearSize, TEAR_DELAY + tearSize);
        float y2 = uvy - y1;
        if (y2 < 0.0 && y2 > -tearSize) {
            texcoord.y = uvy = fract(y1);
        }
    }

    // Bloom from three jittered samples on six rows round the pixel; the brighter the picture, the more it blurs.
    float bloomSize = float(CTRVCR_BLOOM);
    float blurSize = float(CTRVCR_BLUR);
    vec3 centreColor = ctrSample(texcoordScaled);
    vec3 bloom = centreColor;
    float blur = blurSize;
    if (bloomSize > 0.0) {
        vec3 sum = vec3(0.0);
        for (int i = -3; i < 3; i++) {
            float i3 = float(i) * 3.0;
            float v3 = (0.5 - ctrRandom(texcoordScaled + vec2(1.0 + i3, 0.0) + time)) + float(i);
            float v4 = (0.5 - ctrRandom(texcoordScaled + vec2(2.0 + i3, 0.0) + (time + 100.2)));
            float v5 = (0.5 - ctrRandom(texcoordScaled + vec2(3.0 + i3, 0.0) + (time + 200.5)));
            float v6 = (0.5 - ctrRandom(texcoordScaled + vec2(4.0 + i3, 0.0) + (time + 300.8)));
            sum += ctrSample(texcoordScaled + vec2(-1.0 + v4, v3) * 0.004) * bloomSize;
            sum += ctrSample(texcoordScaled + vec2(v5, v3) * 0.004) * bloomSize;
            sum += ctrSample(texcoordScaled + vec2(1.0 + v6, v3) * 0.004) * bloomSize;
        }
        float weight = all(lessThan(centreColor, vec3(0.3))) ? 0.012
                     : all(lessThan(centreColor, vec3(0.5))) ? 0.009 : 0.0075;
        bloom = mix(centreColor, sum * sum * weight + centreColor, bloomSize);
        float sumSingle = (bloom.r + bloom.g + bloom.b) / 3.0;
        float tanSum = tan(min(sumSingle * 1.5, 1.5707963)) / 64.0;
        blur = clamp(blurSize * tanSum, blurSize, 0.01);
    }

    // The tape's colour smear: red read from the left, blue from the right, green from the middle.
    float vScaled = v * blur;
    float v2Scaled = v2 * blur;
    float vHalf = vScaled / 2.0;
    vec3 sumColor;
    vec3 centre = ctrSample(vec2(uvx - vHalf / 2.0 + v2Scaled / 4.0, uvy - vHalf / 4.0 + v2Scaled / 8.0));
    if (blurSize > 0.0) {
        float blur2 = blur / 4.0;
        vec3 left1 = ctrSample(vec2(uvx - vScaled, uvy + vScaled / 3.0));
        vec3 left2 = ctrSample(vec2(uvx - vHalf + left1.r * blur2, uvy + vScaled / 6.0 + left1.g * blur2));
        vec3 right1 = ctrSample(vec2(uvx + vScaled, uvy + vScaled));
        float right2Offset = vHalf - right1.r * blur2;
        vec3 right2 = ctrSample(vec2(uvx + right2Offset, uvy + right2Offset));
        if (bloomSize > 0.0) { centre = centre / 2.0 + bloom / 2.0; }
        sumColor = vec3((left1.r + left2.r + centre.r) / 3.0, centre.g * (1.0 - bloomSize / 4.0),
                        (right1.b + right2.b + centre.b) / 3.0);
    } else {
        if (bloomSize > 0.0) { centre = centre / 2.0 + bloom / 2.0; }
        sumColor = centre;
    }

    // Tint (CTR VCR's tape is a little green), grain, black and white.
    sumColor *= vec3(1.0, float(CTRVCR_TINT_GREEN), 1.0);
    sumColor += sumColor * (0.5 - v) * float(CTRVCR_GRAIN);
    sumColor = mix(sumColor, vec3((sumColor.r + sumColor.g + sumColor.b) / 3.0), float(CTRVCR_BLACK_WHITE));

    // Static, and a row of it that tears across now and then.
    float staticAmount = float(CTRVCR_STATIC);
    const float STATIC_TEAR_CHANCE = 270.4;
    float l = abs(vRow - texcoord.y) * STATIC_TEAR_CHANCE;
    if (l < 0.004 * v) { staticAmount = 1.0 - min(l, 0.05 * v) * 50.0 / v; }
    if (v < staticAmount) {
        float sv = (staticAmount + 0.5) * v2;
        sumColor = (sumColor + vec3(sv)) * (1.0 - staticAmount) + vec3(v * staticAmount);
    }

    float saturation = float(CTRVCR_SATURATION);
    float luma = dot(sumColor, vec3(0.299, 0.587, 0.114));
    sumColor = luma + (sumColor - luma) * saturation;

    // Colour depth with ordered dither (COLOR_RESOLUTION, COLOR_DITERING 2); 0 keeps every colour.
    int levels = int(CTRVCR_COLOR_LEVELS);
    if (levels > 1) {
        float dit = ctrDither(gl_FragCoord.xy) - 0.5;
        float n = float(levels);
        sumColor = levels <= 4 ? floor((sumColor * 0.9 + 0.1) * n + dit) / (n - 1.0)
                               : floor(sumColor * n + dit) / (n - 1.0);
    }

    // Dark edges: the tube's walls.
    float darkEdges = float(CTRVCR_DARK_EDGES);
    if (darkEdges > 0.0) {
        float a = uvIn.x * 100.0 - darkEdges;
        sumColor = max(sumColor, 0.0);
        sumColor *= min(a * 3.0 + v * 0.2, 2.0) - min(a * a + v2 * 0.2, 1.0);
        a = (1.0 - uvIn.x) * 150.0 - floor(darkEdges * 1.5);
        sumColor *= min(a + v2 * 0.2, 1.0);
    }
    // Outside the curved glass: black.
    if (any(lessThan(texcoord, vec2(0.0))) || any(greaterThan(texcoord, vec2(1.0)))) { sumColor = vec3(0.0); }

    // Scanlines (composite1.fsh): bright and dark rows. Above one pixel a line is a sine over lineSize pixels, so a
    // bright and a dark row make one period; CTR VCR's own sine was four times longer, too coarse at these sizes.
    float scanlines = float(CTRVCR_SCANLINES);
    if (scanlines > 0.0) {
        int sizeSetting = int(CTRVCR_SCANLINE_SIZE);
        float lineSize = sizeSetting > 0 ? float(sizeSetting) : max(1.0, floor(size.y / 540.0 + 0.5));
        float scanline;
        if (lineSize > 1.0) {
            float s = sin(gl_FragCoord.y * 3.1415927 / lineSize);
            float m = s * s * scanlines;
            scanline = s > 0.0 ? 1.0 + m : 1.0 - m;
        } else {
            scanline = mod(gl_FragCoord.y, 2.0) > 1.0 ? 1.0 + scanlines / 2.0 : 1.0 - scanlines / 2.0;
        }
        sumColor *= scanline;
    }
    color = clamp(sumColor, 0.0, 1.0);
}

#endif
