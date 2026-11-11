// Part of main() in world/world.rgen: included there, in the middle of the function, and nowhere else. It is not a
// file of functions and has no include guard: it runs inside main() and shares its local variables with the parts
// before and after it.
//
// The incident light meter the exposure pass reads: at the centre of each cell of a 16 x 16 grid, the light reaching
// the surface seen there, and the daylight and darkness around the camera.

    // Incident light meter, as Bedrock RTX meters: at the centre of each cell of a 16 x 16 grid, the light reaching
    // the surface seen there, as the brightness a white diffuse surface would have under it (the surface's own
    // colour divided out). Stored as the cube root per channel, for the power mean the exposure pass takes, and a
    // weight that fades distant surfaces out; the sky, glass, water and the hand count nothing.
    {
        ivec2 meterCell = max(ivec2(gl_LaunchSizeEXT.xy) / 16, ivec2(1));
        ivec2 meterIndex = pixel / meterCell;
        if (all(lessThan(meterIndex, ivec2(16))) && all(equal(pixel, meterIndex * meterCell + meterCell / 2))) {
            vec4 meterSample = vec4(0.0);
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
            // As Bedrock RTX weights them: in full to 21/32 of the render distance, fading out to none at it (its
            // distance fade, calibrated to Bedrock RTX).
            float renderDistance = max(worldUBO.fogEnd, 32.0);
            float meterWeight = 1.0 - clamp(surfaceDepth * 32.0 / (11.0 * renderDistance) - 21.0 / 11.0, 0.0, 1.0);
#else
            float meterWeight = 1.0 - clamp((surfaceDepth - 32.0) / 96.0, 0.0, 1.0);
#endif
            vec3 white = vec3(-1.0); // Negative: no reading this frame.
            if (!psrHitSky && isFirstHitDiffuse && isHand == 0) {
                vec3 albedo = max(firstHitMat.albedoValue.rgb * firstHitMat.albedoValue.a, vec3(0.04));
                vec3 surfaceLight = firstHitDirectRadiance + firstHitIndirectRadiance;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
                // Bedrock meters the light falling on the surface, not the light it gives off: its own glow is no
                // part of it (a lamp in view read as bright as the glow it shows, not as the light it stands in).
                surfaceLight -= firstHitEmission;
#endif
                vec3 reading = max(surfaceLight / albedo, vec3(0.0));
                if (!any(isnan(reading)) && !any(isinf(reading))) { white = reading; }
            }
            // In lava or powder snow the camera sees nothing but the medium, so the medium is what it meters
            // (cameraMedium): the light on the surfaces hidden behind it opened the eye wide in a dark cave and burnt
            // lava to a bright orange, where by day it read dark red.
            bool meterMedium = cameraInMedium;
            if (meterMedium) {
                white = cameraMediumMeter;
                meterWeight = 1.0;
            }
            uint meterSlot = uint(meterIndex.y * 16 + meterIndex.x);
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
            // Bedrock meters the light its denoiser leaves, not one path's: each sample here is accumulated over the
            // frames its surface stays under the cell, as that denoiser accumulates - all of it on a surface just come
            // into view, 1/n of the way on its n-th frame, never less than a fiftieth (its diffuse temporal alpha,
            // calibrated to Bedrock RTX). The surface is the same while it faces the same way and lies on the same
            // plane within Bedrock's tolerance (0.025 blocks and half a percent of the distance). The cube roots of
            // single paths averaged low where the light comes in rare bright paths, and read the place darker.
            vec4 history = lightMeter.sampleHistory[meterSlot];
            vec4 historySurface = lightMeter.sampleSurface[meterSlot];
            if (meterMedium) {
                // The medium's light is the same every frame: nothing to accumulate, and no surface to follow.
                lightMeter.sampleHistory[meterSlot] = vec4(white, 1.0);
                lightMeter.sampleSurface[meterSlot] = vec4(0.0);
                meterSample = vec4(exp2(log2(max(white, vec3(1e-12))) / 3.0), meterWeight);
            } else if (!psrHitSky && isHand == 0 && isFirstHitNoisy && !useTransparentSplit) {
                vec3 axisSize = abs(firstHitNormal);
                int axisIndex = axisSize.x >= axisSize.y && axisSize.x >= axisSize.z ? 0 :
                                (axisSize.y >= axisSize.z ? 1 : 2);
                vec3 axis = vec3(0.0);
                axis[axisIndex] = firstHitNormal[axisIndex] > 0.0 ? 1.0 : -1.0;
                float axisCode = float(axisIndex * 2 + (axis[axisIndex] > 0.0 ? 1 : 2));
                vec3 previousPoint = historySurface.xyz - cameraDelta;
                bool kept = history.w >= 1.0 && history.w <= 100.0 && historySurface.w == axisCode &&
                            !any(isnan(history)) && !any(isinf(history)) && !any(isnan(previousPoint)) &&
                            abs(dot(axis, firstHitWorldPos - previousPoint)) <= 0.005 * surfaceDepth + 0.025;
                if (white.x >= 0.0) {
                    float frames = kept ? min(history.w + 1.0, 100.0) : 1.0;
                    vec3 accumulated = kept ? mix(history.rgb, white, max(1.0 / frames, 0.02)) : white;
                    lightMeter.sampleHistory[meterSlot] = vec4(accumulated, frames);
                    lightMeter.sampleSurface[meterSlot] = vec4(firstHitWorldPos, axisCode);
                    meterSample = vec4(exp2(log2(max(accumulated, vec3(1e-12))) / 3.0), meterWeight);
                } else if (kept) {
                    // This frame's path took the surface's reflection, which says nothing of the light on it: the
                    // light gathered so far stands in.
                    meterSample = vec4(exp2(log2(max(history.rgb, vec3(1e-12))) / 3.0), meterWeight);
                } else {
                    lightMeter.sampleHistory[meterSlot] = vec4(0.0);
                }
            } else {
                lightMeter.sampleHistory[meterSlot] = vec4(0.0);
            }
            lightMeter.samples[meterSlot] = meterSample;
#else
            if (white.x >= 0.0) { meterSample = vec4(exp2(log2(max(white, vec3(1e-12))) / 3.0), meterWeight); }
            // Averaged over frames: each sample is one noisy path, and the meter shimmered with it.
            vec4 previousSample = lightMeter.samples[meterSlot];
            bool usable = !any(isnan(previousSample)) && !any(isinf(previousSample));
            lightMeter.samples[meterSlot] = usable ? mix(previousSample, meterSample, 0.2) : meterSample;
#endif
        }
        // How much of a day it is, by the sun's height: the eye may open far by day (a room, a cave) but not at
        // night, which is meant to stay dark. Other dimensions have no night.
        if (all(equal(pixel, ivec2(0)))) {
            // The End is a dark place by design: the eye stays as at night there, or its dim void was opened up to
            // a pale pink glare.
            // The Nether too: lit by lava alone, the eye let open by day brought its fog up to a flat pink sky.
            float day = worldUBO.skyType == 1 ? smoothstep(-0.05, 0.15, celestialSunDirection().y) : 0.0;
            // And how dark the place is meant to look: the End is kept well below what the meter would bring it
            // to, a dim world against a near-black void as in vanilla.
            float darkness = worldUBO.skyType == 2 ? END_EXPOSURE_SCALE : (worldUBO.skyType == 0 ? NETHER_EXPOSURE_SCALE : 1.0);
            // z: the camera is under water. The eye holds the exposure it had there (exposure.comp): the water
            // darkens with depth on its own (water_medium.glsl), and an eye adapting to it brought the brightness
            // back as the camera sank - dimming, then opening up again.
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
            // Bedrock RTX's eye goes on metering under water: going in from a sunny pool, its picture brightens by
            // about a stop over a second and a half (observed), and the water reads a bright blue.
            float underWater = 0.0;
#else
            float underWater = skyUBO.cameraSubmersionType == 1 ? 1.0 : 0.0;
#endif
            // The picture a little under what the meter brings it to, by day and by night (settings); w: how far the
            // eye may open by day at most, so a sealed room stays dark instead of being opened up to its faint
            // leftover light.
#if !defined(VPT_BEDROCK_ORIGINAL) || VPT_BEDROCK_ORIGINAL == 0
            // (The experimental Bedrock profile exposes as Bedrock RTX does, with no offset of its own.)
            darkness *= mix(VPT_EXPOSURE_NIGHT_SCALE, VPT_EXPOSURE_DAY_SCALE, day);
#else
            // By day a little under Bedrock's: with the sun's light cut (shadow.rmiss) the meter alone opened the eye
            // back up and the picture came out as bright as before, only flatter; held under, daylight is softer.
            darkness *= mix(1.0, BEDROCK_DAY_EXPOSURE_SCALE, day);
#endif
            // In lava or powder snow the eye is held below what the medium meters (cameraMedium), by day or night and
            // in any dimension alike.
            if (cameraInMedium) { darkness = cameraMediumExposure; }
            // w: a negative one marks the Nether for the tone mapper (exposure.comp); the other dimensions' day maximum.
            lightMeter.daylight = vec4(day, darkness, underWater, worldUBO.skyType == 0 ? -1.0 : VPT_EXPOSURE_DAY_MAX);
        }
    }
