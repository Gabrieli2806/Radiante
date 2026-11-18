package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.PipelineSettings;
import com.g2806.radiante.client.pipeline.Presets;
import com.g2806.radiante.client.pipeline.RtAttributes;
import com.g2806.radiante.client.proxy.vulkan.RendererProxy;
import com.g2806.radiante.client.render.FrameGeneration;
import com.g2806.radiante.client.render.OutputResolution;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.g2806.radiante.client.render.TracedTextureDetail;
import java.util.Locale;
import java.util.Objects;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import net.minecraft.client.Minecraft;

/**
 * Automatic hardware and live GPU frame-time tuner ("Optimize for my GPU").
 *
 * <p>Combines available denoiser/upscaler modules, physical and driver-budgeted VRAM, window pixel count, and
 * hardware GPU timestamp measurements (normalized by the relative cost of whichever settings were active during the
 * probe, so running it repeatedly converges to the same tier rather than oscillating) to pick the optimal pipeline,
 * quality preset, render distance, texture detail, Reflex and automatic chunk building.
 */
public final class AutoTune {

    private static final Pattern LEADING_MS = Pattern.compile("^\\s*([0-9]+(?:\\.[0-9]+)?)\\s*ms\\b");

    private AutoTune() {
    }

    /** Hardware capabilities, VRAM budget and optional live GPU pass timing snapshot. */
    public record Probe(
        String gpuName,
        boolean nvidia,
        boolean intel,
        boolean dlssRrAvailable,
        boolean dlssSrAvailable,
        boolean xessAvailable,
        boolean fsrAvailable,
        boolean reflexSupported,
        int windowWidth,
        int windowHeight,
        long vramUsedMb,
        long vramBudgetMb,
        long vramTotalMb,
        float measuredGpuMs,
        double currentLoadFactor
    ) {
    }

    /** Chosen settings produced by {@link #recommend(Probe)}. */
    public record Result(
        Presets preset,
        boolean rayReconstruction,
        QualityPreset quality,
        int renderDistance,
        int tracedTextureDetail,
        boolean reflex,
        float measuredGpuMs,
        float normalizedGpuMs,
        long effectiveVramMb,
        boolean liveMeasured
    ) {
    }

    /** Recommends optimal settings from a hardware and timing probe. Pure and deterministic for unit testing. */
    public static Result recommend(Probe probe) {
        Presets preset;
        boolean rayReconstruction = false;
        if (probe.dlssRrAvailable()) {
            preset = Presets.RT_DLSSRR;
            rayReconstruction = true;
        } else if (probe.dlssSrAvailable()) {
            preset = Presets.RT_NRD_DLSS;
        } else if (probe.intel() && probe.xessAvailable()) {
            preset = Presets.RT_NRD_XESS;
        } else if (probe.fsrAvailable()) {
            preset = Presets.RT_NRD_FSR;
        } else if (probe.xessAvailable()) {
            preset = Presets.RT_NRD_XESS;
        } else {
            preset = Presets.RT_NRD;
        }

        boolean live = probe.measuredGpuMs() > 0.05f;
        double loadFactor = Math.clamp(probe.currentLoadFactor(), 0.30, 3.50);
        // NRD + spatial/temporal upscaler is ~15% heavier at equal internal scale than DLSS Ray Reconstruction.
        double denoiserAdjust = (!rayReconstruction && probe.dlssRrAvailable()) ? 0.88 : 1.0;
        float normalizedMs = live
            ? (float) ((probe.measuredGpuMs() / loadFactor) * denoiserAdjust)
            : estimateNormalizedMsFromHardware(probe.gpuName(), probe.nvidia(), probe.intel(),
                probe.windowWidth(), probe.windowHeight(), probe.vramTotalMb(), probe.vramBudgetMb());

        QualityPreset quality;
        if (normalizedMs <= 7.2f) {
            quality = QualityPreset.ULTRA;
        } else if (normalizedMs <= 12.5f) {
            quality = QualityPreset.HIGH;
        } else if (normalizedMs <= 18.5f) {
            quality = QualityPreset.MEDIUM;
        } else {
            quality = QualityPreset.LOW;
        }

        int renderDistance = quality.renderDistance;
        int textureDetail = TracedTextureDetail.AUTO;
        long effectiveVram = effectiveVramMb(probe);

        if (effectiveVram > 0 && effectiveVram < 5600) {
            if (quality == QualityPreset.ULTRA || quality == QualityPreset.HIGH) {
                quality = QualityPreset.MEDIUM;
            }
            renderDistance = Math.min(quality.renderDistance, 10);
            textureDetail = TracedTextureDetail.HALF;
        } else if (effectiveVram > 0 && effectiveVram < 8600) {
            if (quality == QualityPreset.ULTRA) {
                quality = QualityPreset.HIGH;
            }
            renderDistance = Math.min(quality.renderDistance, 14);
        } else if (effectiveVram > 0 && effectiveVram < 10500 && quality == QualityPreset.ULTRA) {
            renderDistance = Math.min(quality.renderDistance, 18);
        }

        return new Result(
            preset,
            rayReconstruction,
            quality,
            renderDistance,
            textureDetail,
            probe.reflexSupported(),
            live ? probe.measuredGpuMs() : 0.0f,
            normalizedMs,
            effectiveVram,
            live
        );
    }

    private static long effectiveVramMb(Probe probe) {
        if (probe.vramBudgetMb() > 0 && probe.vramTotalMb() > 0) {
            return Math.min(probe.vramBudgetMb(), probe.vramTotalMb());
        }
        return probe.vramBudgetMb() > 0 ? probe.vramBudgetMb() : probe.vramTotalMb();
    }

    /** Fallback estimate when run from the menus before any world frame has been traced. */
    static float estimateNormalizedMsFromHardware(String gpuName, boolean nvidia, boolean intel,
        int windowWidth, int windowHeight, long vramTotalMb, long vramBudgetMb) {
        String name = gpuName == null ? "" : gpuName.toUpperCase(Locale.ROOT);
        float base1080pMs = 13.5f;

        if (containsAny(name, "5090", "5080", "4090", "4080", "7900 XTX")) {
            base1080pMs = 4.6f;
        } else if (containsAny(name, "5070 TI", "4070 TI", "4070 SUPER", "3090", "3080 TI", "9070 XT", "7900 XT")) {
            base1080pMs = 6.2f;
        } else if (containsAny(name, "5070", "4070", "3080", "9070", "7800 XT", "7900 GRE", "6950", "6900")) {
            base1080pMs = 7.8f;
        } else if (containsAny(name, "5060 TI", "4060 TI", "3070", "3060 TI", "7700 XT", "6800", "B580")) {
            base1080pMs = 10.2f;
        } else if (containsAny(name, "5060", "4060", "3060", "2080", "2070", "7600", "6750", "6700", "A770", "B570")) {
            base1080pMs = 13.5f;
        } else if (containsAny(name, "3050", "2060", "6650", "6600", "6500", "A750", "A580", "A380")) {
            base1080pMs = 19.5f;
        } else {
            long mem = vramTotalMb > 0 ? vramTotalMb : vramBudgetMb;
            if (mem >= 15000) base1080pMs = nvidia ? 6.5f : 8.5f;
            else if (mem >= 11000) base1080pMs = nvidia ? 8.5f : 10.8f;
            else if (mem >= 7500) base1080pMs = intel ? 12.5f : 11.8f;
            else if (mem > 0) base1080pMs = 19.5f;
        }

        double pixels = Math.max(640.0 * 360.0, (double) Math.max(1, windowWidth) * Math.max(1, windowHeight));
        double resFactor = Math.pow(pixels / (1920.0 * 1080.0), 0.82);
        return (float) (base1080pMs * resFactor);
    }

    private static boolean containsAny(String haystack, String... needles) {
        for (String needle : needles) {
            if (haystack.contains(needle)) {
                return true;
            }
        }
        return false;
    }

    /** Builds a {@link Probe} from the running client and renderer state. */
    public static Probe collectCurrentProbe(Minecraft minecraft, float liveMeasuredGpuMs) {
        long[] stats = Pipeline.sceneStats();
        long usedMb = stats != null && stats.length > 2 ? stats[2] : 0L;
        long budgetMb = stats != null && stats.length > 3 ? stats[3] : 0L;
        long totalMb = 0L;
        try {
            totalMb = RendererProxy.totalDeviceMemoryMb();
        } catch (UnsatisfiedLinkError ignored) {
        }

        int winW = minecraft != null && minecraft.getWindow() != null ? minecraft.getWindow().getWidth() : 1920;
        int winH = minecraft != null && minecraft.getWindow() != null ? minecraft.getWindow().getHeight() : 1080;

        float gpuMs = liveMeasuredGpuMs;
        if (gpuMs <= 0.05f && minecraft != null && minecraft.level != null && RadianteRenderer.levelFrames() >= 60) {
            gpuMs = parseSummaryMs(Pipeline.gpuStats());
        }

        return new Probe(
            RadianteRenderer.gpuIdentity(),
            RadianteRenderer.isNvidiaGpu(),
            RadianteRenderer.isIntelGpu(),
            Pipeline.isPresetAvailable(Presets.RT_DLSSRR.key),
            Pipeline.isPresetAvailable(Presets.RT_NRD_DLSS.key),
            Pipeline.isPresetAvailable(Presets.RT_NRD_XESS.key),
            Pipeline.isPresetAvailable(Presets.RT_NRD_FSR.key),
            FrameGeneration.isReflexSupported(),
            winW,
            winH,
            usedMb,
            budgetMb,
            totalMb,
            gpuMs,
            computeActiveLoadFactor(winW, winH)
        );
    }

    static float parseSummaryMs(String summary) {
        if (summary == null || summary.isBlank()) {
            return 0.0f;
        }
        Matcher m = LEADING_MS.matcher(summary);
        if (m.find()) {
            try {
                return Float.parseFloat(m.group(1));
            } catch (NumberFormatException ignored) {
            }
        }
        return 0.0f;
    }

    /** Relative GPU cost of the active pipeline settings compared to Balanced upscaling + High shading (1.0). */
    public static double computeActiveLoadFactor(int winW, int winH) {
        double scaleFactor = 1.0;
        String activePreset = Pipeline.INSTANCE.getActivePresetName();
        if (Objects.equals(activePreset, Presets.RT_DLSSRR.key)
            || Objects.equals(activePreset, Presets.RT_NRD_DLSS.key)) {
            int custom = parsePositiveInt(PipelineSettings.getModuleValue(
                Pipeline.DLSS_MODULE_NAME, PipelineSettings.DLSS_RENDER_SCALE_ATTRIBUTE));
            if (custom > 0) {
                double r = custom / 100.0;
                scaleFactor = 0.25 + 2.10 * r * r;
            } else {
                int idx = PipelineSettings.DLSS_MODES.indexOf(PipelineSettings.getDlssMode());
                scaleFactor = modeIndexLoadFactor(idx);
            }
        } else if (Objects.equals(activePreset, Presets.RT_NRD_FSR.key)) {
            int custom = parsePositiveInt(PipelineSettings.getModuleValue(
                PipelineSettings.FSR_MODULE_NAME, PipelineSettings.FSR_RENDER_SCALE_ATTRIBUTE));
            if (custom > 0) {
                double r = custom / 100.0;
                scaleFactor = 0.28 + 2.15 * r * r;
            } else {
                String mode = PipelineSettings.getModuleValue(
                    PipelineSettings.FSR_MODULE_NAME, PipelineSettings.FSR_MODE_ATTRIBUTE);
                scaleFactor = modeIndexLoadFactor(PipelineSettings.FSR_MODES.indexOf(mode)) * 1.12;
            }
        } else if (Objects.equals(activePreset, Presets.RT_NRD_XESS.key)) {
            int custom = parsePositiveInt(PipelineSettings.getModuleValue(
                PipelineSettings.XESS_MODULE_NAME, PipelineSettings.XESS_RENDER_SCALE_ATTRIBUTE));
            if (custom > 0) {
                double r = custom / 100.0;
                scaleFactor = 0.28 + 2.15 * r * r;
            } else {
                String mode = PipelineSettings.getModuleValue(
                    PipelineSettings.XESS_MODULE_NAME, PipelineSettings.XESS_MODE_ATTRIBUTE);
                scaleFactor = modeIndexLoadFactor(PipelineSettings.XESS_MODES.indexOf(mode)) * 1.10;
            }
        } else {
            // Unupscaled native NRD.
            scaleFactor = 2.35;
        }

        int[] outSize = OutputResolution.byName(Options.outputResolution)
            .outputSize(Math.max(1, winW), Math.max(1, winH), Options.outputResolutionPercent);
        double outRatio = ((double) outSize[0] * outSize[1]) / ((double) Math.max(1, winW) * Math.max(1, winH));
        scaleFactor *= Math.pow(Math.clamp(outRatio, 0.25, 1.0), 0.85);

        int bounces = parsePositiveInt(PipelineSettings.getShaderPackValue(RtAttributes.RAY_BOUNCES_ATTRIBUTE));
        if (bounces <= 0) bounces = 3;
        if (PipelineSettings.supportsShaderPackToggle(RtAttributes.BEDROCK_ONE_BOUNCE_ATTRIBUTE)
            && PipelineSettings.isShaderPackToggleOn(RtAttributes.BEDROCK_ONE_BOUNCE_ATTRIBUTE)) {
            bounces = 1;
        }
        double bounceFactor = switch (bounces) {
            case 1 -> 0.65;
            case 2 -> 0.82;
            case 3 -> 1.00;
            case 4 -> 1.22;
            default -> 1.22 + 0.10 * (bounces - 4);
        };

        boolean fogOn = !PipelineSettings.supportsVolumetricFog() || PipelineSettings.isVolumetricFog();
        boolean froxel = !PipelineSettings.supportsShaderPackToggle(RtAttributes.FROXEL_FOG_ATTRIBUTE)
            || PipelineSettings.isShaderPackToggleOn(RtAttributes.FROXEL_FOG_ATTRIBUTE);
        int fogSamples = parsePositiveInt(PipelineSettings.getShaderPackValue(RtAttributes.VOLUMETRIC_SAMPLES_ATTRIBUTE));
        double fogFactor = !fogOn ? 0.90 : froxel ? 1.00 : (0.94 + 0.012 * Math.max(8, fogSamples));

        String clouds = PipelineSettings.getCloudMode();
        double cloudFactor = clouds != null && clouds.endsWith(".volumetric") ? 1.08
            : clouds != null && clouds.endsWith(".off") ? 0.96 : 1.00;

        double blockLightFactor = (Options.collectChunkEmission && Options.blockLightSampling) ? 1.00 : 0.84;

        return Math.clamp(scaleFactor * bounceFactor * fogFactor * cloudFactor * blockLightFactor, 0.30, 3.50);
    }

    private static double modeIndexLoadFactor(int idx) {
        return switch (idx) {
            case 0 -> 0.48; // Ultra Performance
            case 1 -> 0.78; // Performance
            case 2 -> 1.00; // Balanced
            case 3 -> 1.30; // Quality
            default -> idx >= 4 ? 2.15 : 1.00; // DLAA / Ultra Quality / Native
        };
    }

    private static int parsePositiveInt(String raw) {
        if (raw == null) {
            return 0;
        }
        try {
            return (int) Math.round(Double.parseDouble(raw.trim()));
        } catch (NumberFormatException e) {
            return 0;
        }
    }
}
