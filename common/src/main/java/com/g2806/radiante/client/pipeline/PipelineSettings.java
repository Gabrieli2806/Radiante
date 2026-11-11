package com.g2806.radiante.client.pipeline;

import net.minecraft.client.resources.language.I18n;
import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.config.AttributeConfig;
import java.util.List;
import java.util.Map;
import java.util.Objects;

import static com.g2806.radiante.client.pipeline.Pipeline.*;
import static com.g2806.radiante.client.pipeline.PipelineBuild.*;

/**
 * What the settings read and set in the pipeline: the DLSS mode, the clouds, the volumetric fog, and any attribute of
 * the shader pack or of a module, by name. Each setter says whether the value changed; {@link Pipeline} then knows
 * from the attribute whether that takes a build or can be handed to the renderer live.
 *
 * <p>These were part of {@code Pipeline}, which keeps the pipeline itself: its modules and connections, building it,
 * storing it, and the native methods.
 */
public final class PipelineSettings {

    private PipelineSettings() {
    }

    public static final String DLSS_MODE_ATTRIBUTE = "render_pipeline.module.dlss.attribute.mode";
    // Ultra Performance runs as Performance on small windows; see DLSSModule for the render size floor.
    public static final List<String> DLSS_MODES = List.of(
            "render_pipeline.module.dlss.attribute.mode.ultra_performance",
            "render_pipeline.module.dlss.attribute.mode.performance",
            "render_pipeline.module.dlss.attribute.mode.balanced",
            "render_pipeline.module.dlss.attribute.mode.quality",
            "render_pipeline.module.dlss.attribute.mode.dlaa");

    /**
     * The upscaler of the active pipeline and its quality mode, for the debug screen: "DLSS Balanced + Ray
     * Reconstruction", "DLSS Balanced (NRD denoiser)", "FSR3 Upscaler Quality", or "Native" when nothing upscales.
     */
    public static String describeUpscaler() {
        String upscaler = describePipelineUpscaler();
        int[] size = com.g2806.radiante.client.render.RadianteRenderer.lastOutputSize();
        if (size[0] > 0 && (size[0] != size[2] || size[1] != size[3])) {
            // A lower output resolution: what the pipeline upscales to, then FSR 1 to the window.
            return upscaler + " to " + size[0] + "x" + size[1] + ", FSR 1 to " + size[2] + "x" + size[3];
        }
        return upscaler;
    }

    static String describePipelineUpscaler() {
        for (Module module : INSTANCE.modules) {
            if (module == null) {
                continue;
            }
            String modeAttribute = switch (module.name) {
                case DLSS_MODULE_NAME -> DLSS_MODE_ATTRIBUTE;
                case FSR_MODULE_NAME -> FSR_MODE_ATTRIBUTE;
                case XESS_MODULE_NAME -> XESS_MODE_ATTRIBUTE;
                default -> null;
            };
            if (modeAttribute == null) {
                continue;
            }
            StringBuilder description = new StringBuilder(I18n.get(module.name));
            AttributeConfig mode = findAttribute(module, modeAttribute);
            if (mode != null && mode.value != null) {
                description.append(' ').append(I18n.get(mode.value));
            }
            if (Objects.equals(module.name, DLSS_MODULE_NAME)) {
                AttributeConfig rayReconstruction = findAttribute(module, RAY_RECONSTRUCTION_ATTRIBUTE);
                if (rayReconstruction != null && Objects.equals(rayReconstruction.value, "render_pipeline.true")) {
                    description.append(" + ").append(I18n.get(RAY_RECONSTRUCTION_ATTRIBUTE));
                } else {
                    // Said outright: without it the line read the same as DLSS with a missing suffix.
                    description.append(" (NRD denoiser)");
                }
            }
            return description.toString();
        }
        return "Native";
    }

    /** The DLSS quality mode of the active pipeline, or null when it has no DLSS module. */
    public static String getDlssMode() {
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, DLSS_MODULE_NAME)) {
                AttributeConfig mode = findAttribute(module, DLSS_MODE_ATTRIBUTE);
                return mode == null ? null : mode.value;
            }
        }
        return null;
    }

    static void setRayReconstructionOn(Module dlssModule, boolean enabled) {
        AttributeConfig attribute = findAttribute(dlssModule, RAY_RECONSTRUCTION_ATTRIBUTE);
        if (attribute != null) {
            assign(attribute, enabled ? "render_pipeline.true" : "render_pipeline.false");
        }
    }

    public static final String RAY_RECONSTRUCTION_ATTRIBUTE =
            "render_pipeline.module.dlss.attribute.ray_reconstruction";

    /** Sets the DLSS quality mode; returns true when the pipeline needs rebuilding. */
    public static boolean setDlssMode(String mode) {
        boolean changed = false;
        for (Module module : INSTANCE.modules) {
            if (module == null || !Objects.equals(module.name, DLSS_MODULE_NAME)) {
                continue;
            }
            AttributeConfig attribute = findAttribute(module, DLSS_MODE_ATTRIBUTE);
            if (attribute != null && !Objects.equals(attribute.value, mode)) {
                logSettingChange(DLSS_MODE_ATTRIBUTE, attribute.value, mode);
                assign(attribute, mode);
                changed = true;
            }
        }
        return changed;
    }

    public static final String FSR_MODULE_NAME = "render_pipeline.module.fsr_upscaler.name";
    /**
     * The share of the screen each upscaler renders at, in percent; 0 leaves it to the upscaler's mode. Every
     * upscaler module has one.
     */
    public static final String DLSS_RENDER_SCALE_ATTRIBUTE = "render_pipeline.module.dlss.attribute.render_scale";
    public static final String FSR_RENDER_SCALE_ATTRIBUTE = "render_pipeline.module.fsr_upscaler.attribute.render_scale";
    public static final String XESS_RENDER_SCALE_ATTRIBUTE = "render_pipeline.module.xess_sr.attribute.render_scale";
    public static final String FSR_MODE_ATTRIBUTE = "render_pipeline.module.fsr_upscaler.attribute.quality_mode";
    /**
     * The render resolution modes of the FSR and XeSS upscalers, from full resolution down; the renderer draws at
     * the mode's share of the screen and upscales the rest. Index-aligned with the quality levels (QualityPreset).
     */
    public static final List<String> FSR_MODES = List.of(
        "render_pipeline.module.fsr_upscaler.attribute.quality_mode.ultra",
        "render_pipeline.module.fsr_upscaler.attribute.quality_mode.performance",
        "render_pipeline.module.fsr_upscaler.attribute.quality_mode.balanced",
        "render_pipeline.module.fsr_upscaler.attribute.quality_mode.quality",
        "render_pipeline.module.fsr_upscaler.attribute.quality_mode.native");
    public static final String XESS_MODULE_NAME = "render_pipeline.module.xess_sr.name";
    public static final String XESS_MODE_ATTRIBUTE = "render_pipeline.module.xess_sr.attribute.quality_mode";
    public static final List<String> XESS_MODES = List.of(
        "render_pipeline.module.xess_sr.attribute.quality_mode.ultra_performance",
        "render_pipeline.module.xess_sr.attribute.quality_mode.performance",
        "render_pipeline.module.xess_sr.attribute.quality_mode.balanced",
        "render_pipeline.module.xess_sr.attribute.quality_mode.quality",
        "render_pipeline.module.xess_sr.attribute.quality_mode.ultra_quality",
        "render_pipeline.module.xess_sr.attribute.quality_mode.ultra_quality_plus",
        "render_pipeline.module.xess_sr.attribute.quality_mode.native");

    public static final String CLOUD_MODE_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.cloud_mode";
    /**
     * The modes the renderer can produce: none, Minecraft's own blocky clouds as traced geometry, or the pack's
     * ray marched ones.
     */
    public static final List<String> CLOUD_MODES = List.of(
        "render_pipeline.module.ray_tracing.attribute.cloud_mode.off",
        "render_pipeline.module.ray_tracing.attribute.cloud_mode.vanilla",
        "render_pipeline.module.ray_tracing.attribute.cloud_mode.volumetric");

    static final String CLOUD_TEMPORAL_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.volumetric_cloud_temporal_accumulation";

    /** Sets the cloud mode; returns true when the pipeline needs rebuilding. */
    public static boolean setCloudMode(String mode) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), CLOUD_MODE_ATTRIBUTE);
        if (attribute == null || Objects.equals(attribute.value, mode)) {
            return false;
        }
        assign(attribute, mode);

        // Ray marched clouds are noisy frame to frame; without accumulation across frames they visibly flicker, so
        // turning the clouds on turns this on with them.
        AttributeConfig temporal = findAttribute(getRayTracingModule(), CLOUD_TEMPORAL_ATTRIBUTE);
        if (temporal != null && mode.endsWith(".volumetric")) {
            assign(temporal, "render_pipeline.true");
        }
        return true;
    }

    public static String getCloudMode() {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), CLOUD_MODE_ATTRIBUTE);
        return attribute == null ? null : attribute.value;
    }

    public static boolean supportsClouds() {
        return findAttribute(getRayTracingModule(), CLOUD_MODE_ATTRIBUTE) != null;
    }

    public static final String VOLUMETRIC_LIGHT_ATTRIBUTE =
        "render_pipeline.module.ray_tracing.attribute.volumetric_light_mode";
    static final String VOLUMETRIC_LIGHT_ON = VOLUMETRIC_LIGHT_ATTRIBUTE + ".volumetric";
    static final String VOLUMETRIC_LIGHT_OFF = VOLUMETRIC_LIGHT_ATTRIBUTE + ".vanilla";

    /**
     * Volumetric fog: the shader pack ray marches the air with a shadow ray per step, which is what gives light
     * shafts, and the biome fog becomes part of that medium. Returns true when the pipeline needs rebuilding.
     */
    public static boolean setVolumetricFog(boolean enabled) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), VOLUMETRIC_LIGHT_ATTRIBUTE);
        String value = enabled ? VOLUMETRIC_LIGHT_ON : VOLUMETRIC_LIGHT_OFF;
        if (attribute == null || Objects.equals(attribute.value, value)) {
            return false;
        }
        assign(attribute, value);
        return true;
    }

    public static boolean isVolumetricFog() {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), VOLUMETRIC_LIGHT_ATTRIBUTE);
        return attribute != null && Objects.equals(attribute.value, VOLUMETRIC_LIGHT_ON);
    }

    public static boolean supportsVolumetricFog() {
        return findAttribute(getRayTracingModule(), VOLUMETRIC_LIGHT_ATTRIBUTE) != null;
    }

    /**
     * Whether block lights cast what their Bedrock MER maps show (EmissionTiles.uploadFromMer): by default they show
     * that glow and light the world with it; with either switch off the light levels' scale is kept.
     */
    public static boolean castsBedrockGlow() {
        return isShaderPackToggleOn(RtAttributes.BEDROCK_MATERIALS_ATTRIBUTE)
            && isShaderPackToggleOn(RtAttributes.BEDROCK_ORIGINAL_ATTRIBUTE);
    }

    /** The wind's strength for this frame (WorldUBO.windStrength): the slider, or 0 with wind off or not offered. */
    public static float windStrength() {
        if (!isShaderPackToggleOn(RtAttributes.WIND_ATTRIBUTE)) {
            return 0.0f;
        }
        String value = getShaderPackValue(RtAttributes.WIND_STRENGTH_ATTRIBUTE);
        try {
            return value == null ? 1.0f : Math.max(0.0f, Float.parseFloat(value.trim()));
        } catch (NumberFormatException notANumber) {
            return 1.0f;
        }
    }

    /**
     * Switches one of the shader pack's on/off attributes, such as motion blur or depth of field. Returns true when
     * it changed; nearly all of them are compiled into the pack, so the pipeline needs rebuilding (see
     * takeStructuralChange).
     */
    public static boolean setShaderPackToggle(String attributeName, boolean enabled) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), attributeName);
        String value = enabled ? "render_pipeline.true" : "render_pipeline.false";
        if (attribute == null || Objects.equals(attribute.value, value)) {
            return false;
        }
        logSettingChange(attributeName, attribute.value, value);
        assign(attribute, value);
        return true;
    }

    public static boolean isShaderPackToggleOn(String attributeName) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), attributeName);
        return attribute != null && Objects.equals(attribute.value, "render_pipeline.true");
    }

    /** A shader pack attribute's value as text, or null when the pack does not have it. */
    public static String getShaderPackValue(String attributeName) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), attributeName);
        return attribute == null ? null : attribute.value;
    }

    /**
     * Sets a shader pack attribute; true when it changed. Whether that takes a rebuild or reaches the renderer live
     * is takeStructuralChange's and pushRuntimeChanges' to say.
     */
    public static boolean setShaderPackValue(String attributeName, String value) {
        AttributeConfig attribute = findAttribute(getRayTracingModule(), attributeName);
        if (attribute == null || Objects.equals(attribute.value, value)) {
            return false;
        }
        logSettingChange(attributeName, attribute.value, value);
        assign(attribute, value);
        return true;
    }

    /** With debug logging, which setting asked for a rebuild: a value written over a different one. */
    static void logSettingChange(String attribute, String from, String to) {
        if (Options.debugLogging) {
            RadianteClient.LOGGER.info("[settings] {}: {} -> {}", attribute, from, to);
        }
    }

    /**
     * Every setter's write, so the pipeline knows how the renderer has to learn of it: a runtime attribute's new
     * value waits for pushRuntimeChanges, any other change for a build. True when the value changed.
     */
    static boolean assign(AttributeConfig attribute, String value) {
        if (Objects.equals(attribute.value, value)) {
            return false;
        }
        attribute.value = value;
        if (attribute.runtime) {
            INSTANCE.pendingRuntimeChanges.put(attribute.name, value);
        } else {
            INSTANCE.structuralChange = true;
        }
        return true;
    }

    /** Every attribute of a module of the active pipeline and its value, in order; empty without that module. */
    public static Map<String, String> moduleValues(String moduleName) {
        Map<String, String> values = new java.util.LinkedHashMap<>();
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, moduleName) && module.attributeConfigs != null) {
                for (AttributeConfig attribute : module.attributeConfigs) {
                    if (attribute != null && attribute.name != null) {
                        values.put(attribute.name, attribute.value);
                    }
                }
            }
        }
        return values;
    }

    /** The declared type of a module's attribute ("bool", "int_range:1-8", "enum:a-b"...), or null without it. */
    public static String getModuleAttributeType(String moduleName, String attributeName) {
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, moduleName)) {
                AttributeConfig attribute = findAttribute(module, attributeName);
                return attribute == null ? null : attribute.type;
            }
        }
        return null;
    }

    /** An attribute of any module by module and attribute name, or null when there is no such module or attribute. */
    public static String getModuleValue(String moduleName, String attributeName) {
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, moduleName)) {
                AttributeConfig attribute = findAttribute(module, attributeName);
                return attribute == null ? null : attribute.value;
            }
        }
        return null;
    }

    /** Sets an attribute of any module by module and attribute name; true when it changed. */
    public static boolean setModuleValue(String moduleName, String attributeName, String value) {
        for (Module module : INSTANCE.modules) {
            if (module != null && Objects.equals(module.name, moduleName)) {
                AttributeConfig attribute = findAttribute(module, attributeName);
                if (attribute == null || Objects.equals(attribute.value, value)) {
                    return false;
                }
                logSettingChange(attributeName, attribute.value, value);
                assign(attribute, value);
                return true;
            }
        }
        return false;
    }

    /** A shader pack attribute as a whole number, or {@code fallback} when it is missing or not a number. */
    public static int getShaderPackInt(String attributeName, int fallback) {
        String value = getShaderPackValue(attributeName);
        try {
            return value == null ? fallback : (int) Float.parseFloat(value.trim());
        } catch (NumberFormatException notANumber) {
            return fallback;
        }
    }

    /** False when the active shader pack does not offer the attribute at all. */
    public static boolean supportsShaderPackToggle(String attributeName) {
        return findAttribute(getRayTracingModule(), attributeName) != null;
    }
}
