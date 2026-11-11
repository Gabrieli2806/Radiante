package com.g2806.radiante.client.pipeline;

import static com.g2806.radiante.client.pipeline.Pipeline.DLSS_MODULE_NAME;
import static com.g2806.radiante.client.pipeline.Pipeline.FSR_MODULE_NAME;
import static com.g2806.radiante.client.pipeline.Pipeline.INSTANCE;
import static com.g2806.radiante.client.pipeline.Pipeline.NRD_MODULE_NAME;
import static com.g2806.radiante.client.pipeline.Pipeline.POST_RENDER_MODULE_NAME;
import static com.g2806.radiante.client.pipeline.Pipeline.RAY_TRACING_MODULE_NAME;
import static com.g2806.radiante.client.pipeline.Pipeline.TEMPORAL_ACCUMULATION_MODULE_NAME;
import static com.g2806.radiante.client.pipeline.Pipeline.TONE_MAPPING_MODULE_NAME;
import static com.g2806.radiante.client.pipeline.Pipeline.XESS_MODULE_NAME;
import static com.g2806.radiante.client.pipeline.Pipeline.addModule;
import static com.g2806.radiante.client.pipeline.Pipeline.clear;
import static com.g2806.radiante.client.pipeline.Pipeline.connect;
import static com.g2806.radiante.client.pipeline.Pipeline.connectOutput;
import static com.g2806.radiante.client.pipeline.Pipeline.setRayReconstructionOn;

import java.util.Objects;

/**
 * The modules and connections of each preset ({@link Presets}): which modules a pipeline is made of and which image
 * of one feeds which input of the next. Whether a preset can run here, and which one to fall back to, is
 * {@link Pipeline}'s to decide; this only lays out the one it asks for.
 */
final class PresetAssembly {

    private PresetAssembly() {
    }

    private static void assembleDLSSRRInternal() {
        clear();

        Module rayTracingModule = addModule(RAY_TRACING_MODULE_NAME);

        Module dlssModule = addModule(DLSS_MODULE_NAME);

        Module toneMappingModule = addModule(TONE_MAPPING_MODULE_NAME);

        Module postRenderModule = addModule(POST_RENDER_MODULE_NAME);

        rayTracingModule.x = 100;
        rayTracingModule.y = 220;
        dlssModule.x = 380;
        dlssModule.y = 220;
        toneMappingModule.x = 660;
        toneMappingModule.y = 140;
        postRenderModule.x = 660;
        postRenderModule.y = 300;

        INSTANCE.activePresetName = Presets.RT_DLSSRR.key;
        // DLSS is the denoiser in this preset, so ray reconstruction is what the module has to create. Its super
        // resolution path is RT_NRD_DLSS, where NRD has already denoised the picture.
        setRayReconstructionOn(dlssModule, true);

        connect(rayTracingModule.getOutputImageConfig("radiance"),
                dlssModule.getInputImageConfig("radiance"));

        connect(rayTracingModule.getOutputImageConfig("diffuse_albedo_metallic"),
                dlssModule.getInputImageConfig("diffuse_albedo_metallic"));

        connect(rayTracingModule.getOutputImageConfig("specular_albedo"),
                dlssModule.getInputImageConfig("specular_albedo"));

        connect(rayTracingModule.getOutputImageConfig("normal_roughness"),
                dlssModule.getInputImageConfig("normal_roughness"));

        connect(rayTracingModule.getOutputImageConfig("motion_vector"),
                dlssModule.getInputImageConfig("motion_vector"));

        connect(rayTracingModule.getOutputImageConfig("linear_depth"),
                dlssModule.getInputImageConfig("linear_depth"));

        connect(rayTracingModule.getOutputImageConfig("specular_hit_depth"),
                dlssModule.getInputImageConfig("specular_hit_depth"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_depth"),
                dlssModule.getInputImageConfig("first_hit_depth"));

        connect(dlssModule.getOutputImageConfig("processed"),
                toneMappingModule.getInputImageConfig("denoised_radiance"));
        connect(dlssModule.getOutputImageConfig("processed"),
                postRenderModule.getInputImageConfig("hdr_input"));

        connect(dlssModule.getOutputImageConfig("upscaled_first_hit_depth"),
                postRenderModule.getInputImageConfig("first_hit_depth"));
        connect(dlssModule.getOutputImageConfig("upscaled_motion_vector"),
                postRenderModule.getInputImageConfig("motion_vector"));
        connect(dlssModule.getOutputImageConfig("upscaled_normal_roughness"),
                postRenderModule.getInputImageConfig("normal_roughness"));

        connect(toneMappingModule.getOutputImageConfig("mapped_output"),
                postRenderModule.getInputImageConfig("ldr_input"));

        connectOutput(postRenderModule.getOutputImageConfig("post_rendered"));
    }

    static void assembleNRDFSRInternal() {
        clear();

        Module rayTracingModule = addModule(RAY_TRACING_MODULE_NAME);

        Module denoiserModule = addModule(NRD_MODULE_NAME);

        Module upscalerModule = addModule(FSR_MODULE_NAME);

        Module toneMappingModule = addModule(TONE_MAPPING_MODULE_NAME);

        Module postRenderModule = addModule(POST_RENDER_MODULE_NAME);

        rayTracingModule.x = 100;
        rayTracingModule.y = 220;
        denoiserModule.x = 380;
        denoiserModule.y = 120;
        upscalerModule.x = 660;
        upscalerModule.y = 220;
        toneMappingModule.x = 940;
        toneMappingModule.y = 120;
        postRenderModule.x = 940;
        postRenderModule.y = 300;

        INSTANCE.activePresetName = Presets.RT_NRD_FSR.key;

        connect(rayTracingModule.getOutputImageConfig("first_hit_diffuse_indirect_light"),
                denoiserModule.getInputImageConfig("diffuse_radiance"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_specular"),
                denoiserModule.getInputImageConfig("specular_radiance"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_diffuse_direct_light"),
                denoiserModule.getInputImageConfig("direct_radiance"));

        connect(rayTracingModule.getOutputImageConfig("diffuse_albedo_metallic"),
                denoiserModule.getInputImageConfig("diffuse_albedo"));

        connect(rayTracingModule.getOutputImageConfig("specular_albedo"),
                denoiserModule.getInputImageConfig("specular_albedo"));

        connect(rayTracingModule.getOutputImageConfig("normal_roughness"),
                denoiserModule.getInputImageConfig("normal_roughness"));

        connect(rayTracingModule.getOutputImageConfig("motion_vector"),
                denoiserModule.getInputImageConfig("motion_vector"));

        connect(rayTracingModule.getOutputImageConfig("linear_depth"),
                denoiserModule.getInputImageConfig("linear_depth"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_depth"),
                denoiserModule.getInputImageConfig("diffuseHitDepthImage"));

        connect(rayTracingModule.getOutputImageConfig("specular_hit_depth"),
                denoiserModule.getInputImageConfig("specularHitDepthImage"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_clear"),
                denoiserModule.getInputImageConfig("first_hit_clear"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_base_emission"),
                denoiserModule.getInputImageConfig("first_hit_base_emission"));

        connect(rayTracingModule.getOutputImageConfig("fog_image"),
                denoiserModule.getInputImageConfig("fog_image"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_refraction"),
                denoiserModule.getInputImageConfig("first_hit_refraction"));

        connect(denoiserModule.getOutputImageConfig("denoised_radiance"),
                upscalerModule.getInputImageConfig("color"));

        connect(rayTracingModule.getOutputImageConfig("linear_depth"),
                upscalerModule.getInputImageConfig("depth"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_depth"),
                upscalerModule.getInputImageConfig("first_hit_depth"));

        connect(rayTracingModule.getOutputImageConfig("motion_vector"),
                upscalerModule.getInputImageConfig("motion_vector"));
        connect(rayTracingModule.getOutputImageConfig("normal_roughness"),
                upscalerModule.getInputImageConfig("normal_roughness"));

        connect(upscalerModule.getOutputImageConfig("upscaled_radiance"),
                toneMappingModule.getInputImageConfig("denoised_radiance"));
        connect(upscalerModule.getOutputImageConfig("upscaled_radiance"),
                postRenderModule.getInputImageConfig("hdr_input"));
        connect(upscalerModule.getOutputImageConfig("upscaled_first_hit_depth"),
                postRenderModule.getInputImageConfig("first_hit_depth"));
        connect(upscalerModule.getOutputImageConfig("upscaled_motion_vector"),
                postRenderModule.getInputImageConfig("motion_vector"));
        connect(upscalerModule.getOutputImageConfig("upscaled_normal_roughness"),
                postRenderModule.getInputImageConfig("normal_roughness"));

        connect(toneMappingModule.getOutputImageConfig("mapped_output"),
                postRenderModule.getInputImageConfig("ldr_input"));

        connectOutput(postRenderModule.getOutputImageConfig("post_rendered"));
    }

    /**
     * DLSS without Ray Reconstruction: NRD denoises as in the FSR and XeSS presets and DLSS only upscales its
     * result. For GPUs where Ray Reconstruction costs too much, or where it smears or ghosts.
     */
    private static void assembleNRDDLSSInternal() {
        clear();

        Module rayTracingModule = addModule(RAY_TRACING_MODULE_NAME);

        Module denoiserModule = addModule(NRD_MODULE_NAME);

        Module dlssModule = addModule(DLSS_MODULE_NAME);

        Module toneMappingModule = addModule(TONE_MAPPING_MODULE_NAME);

        Module postRenderModule = addModule(POST_RENDER_MODULE_NAME);

        rayTracingModule.x = 100;
        rayTracingModule.y = 220;
        denoiserModule.x = 380;
        denoiserModule.y = 120;
        dlssModule.x = 660;
        dlssModule.y = 220;
        toneMappingModule.x = 940;
        toneMappingModule.y = 120;
        postRenderModule.x = 940;
        postRenderModule.y = 300;

        INSTANCE.activePresetName = Presets.RT_NRD_DLSS.key;
        // The picture reaching DLSS is already denoised, so it must create super resolution: ray reconstruction
        // would denoise it a second time.
        setRayReconstructionOn(dlssModule, false);

        connect(rayTracingModule.getOutputImageConfig("first_hit_diffuse_indirect_light"),
                denoiserModule.getInputImageConfig("diffuse_radiance"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_specular"),
                denoiserModule.getInputImageConfig("specular_radiance"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_diffuse_direct_light"),
                denoiserModule.getInputImageConfig("direct_radiance"));

        connect(rayTracingModule.getOutputImageConfig("diffuse_albedo_metallic"),
                denoiserModule.getInputImageConfig("diffuse_albedo"));

        connect(rayTracingModule.getOutputImageConfig("specular_albedo"),
                denoiserModule.getInputImageConfig("specular_albedo"));

        connect(rayTracingModule.getOutputImageConfig("normal_roughness"),
                denoiserModule.getInputImageConfig("normal_roughness"));

        connect(rayTracingModule.getOutputImageConfig("motion_vector"),
                denoiserModule.getInputImageConfig("motion_vector"));

        connect(rayTracingModule.getOutputImageConfig("linear_depth"),
                denoiserModule.getInputImageConfig("linear_depth"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_depth"),
                denoiserModule.getInputImageConfig("diffuseHitDepthImage"));

        connect(rayTracingModule.getOutputImageConfig("specular_hit_depth"),
                denoiserModule.getInputImageConfig("specularHitDepthImage"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_clear"),
                denoiserModule.getInputImageConfig("first_hit_clear"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_base_emission"),
                denoiserModule.getInputImageConfig("first_hit_base_emission"));

        connect(rayTracingModule.getOutputImageConfig("fog_image"),
                denoiserModule.getInputImageConfig("fog_image"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_refraction"),
                denoiserModule.getInputImageConfig("first_hit_refraction"));

        connect(denoiserModule.getOutputImageConfig("denoised_radiance"),
                dlssModule.getInputImageConfig("radiance"));

        // Super resolution reads only the colour, motion and depth, plus what it upscales for post render; the
        // G-buffer inputs ray reconstruction guides itself with are connected all the same, so none is unbound.
        connect(rayTracingModule.getOutputImageConfig("diffuse_albedo_metallic"),
                dlssModule.getInputImageConfig("diffuse_albedo_metallic"));

        connect(rayTracingModule.getOutputImageConfig("specular_albedo"),
                dlssModule.getInputImageConfig("specular_albedo"));

        connect(rayTracingModule.getOutputImageConfig("normal_roughness"),
                dlssModule.getInputImageConfig("normal_roughness"));

        connect(rayTracingModule.getOutputImageConfig("motion_vector"),
                dlssModule.getInputImageConfig("motion_vector"));

        connect(rayTracingModule.getOutputImageConfig("linear_depth"),
                dlssModule.getInputImageConfig("linear_depth"));

        connect(rayTracingModule.getOutputImageConfig("specular_hit_depth"),
                dlssModule.getInputImageConfig("specular_hit_depth"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_depth"),
                dlssModule.getInputImageConfig("first_hit_depth"));

        connect(dlssModule.getOutputImageConfig("processed"),
                toneMappingModule.getInputImageConfig("denoised_radiance"));
        connect(dlssModule.getOutputImageConfig("processed"),
                postRenderModule.getInputImageConfig("hdr_input"));

        connect(dlssModule.getOutputImageConfig("upscaled_first_hit_depth"),
                postRenderModule.getInputImageConfig("first_hit_depth"));
        connect(dlssModule.getOutputImageConfig("upscaled_motion_vector"),
                postRenderModule.getInputImageConfig("motion_vector"));
        connect(dlssModule.getOutputImageConfig("upscaled_normal_roughness"),
                postRenderModule.getInputImageConfig("normal_roughness"));

        connect(toneMappingModule.getOutputImageConfig("mapped_output"),
                postRenderModule.getInputImageConfig("ldr_input"));

        connectOutput(postRenderModule.getOutputImageConfig("post_rendered"));
    }

    private static void assembleNRDXESSInternal() {
        clear();

        Module rayTracingModule = addModule(RAY_TRACING_MODULE_NAME);

        Module denoiserModule = addModule(NRD_MODULE_NAME);

        Module upscalerModule = addModule(XESS_MODULE_NAME);

        Module toneMappingModule = addModule(TONE_MAPPING_MODULE_NAME);

        Module postRenderModule = addModule(POST_RENDER_MODULE_NAME);

        rayTracingModule.x = 100;
        rayTracingModule.y = 220;
        denoiserModule.x = 380;
        denoiserModule.y = 120;
        upscalerModule.x = 660;
        upscalerModule.y = 220;
        toneMappingModule.x = 940;
        toneMappingModule.y = 120;
        postRenderModule.x = 940;
        postRenderModule.y = 300;

        INSTANCE.activePresetName = Presets.RT_NRD_XESS.key;

        connect(rayTracingModule.getOutputImageConfig("first_hit_diffuse_indirect_light"),
                denoiserModule.getInputImageConfig("diffuse_radiance"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_specular"),
                denoiserModule.getInputImageConfig("specular_radiance"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_diffuse_direct_light"),
                denoiserModule.getInputImageConfig("direct_radiance"));

        connect(rayTracingModule.getOutputImageConfig("diffuse_albedo_metallic"),
                denoiserModule.getInputImageConfig("diffuse_albedo"));

        connect(rayTracingModule.getOutputImageConfig("specular_albedo"),
                denoiserModule.getInputImageConfig("specular_albedo"));

        connect(rayTracingModule.getOutputImageConfig("normal_roughness"),
                denoiserModule.getInputImageConfig("normal_roughness"));

        connect(rayTracingModule.getOutputImageConfig("motion_vector"),
                denoiserModule.getInputImageConfig("motion_vector"));

        connect(rayTracingModule.getOutputImageConfig("linear_depth"),
                denoiserModule.getInputImageConfig("linear_depth"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_depth"),
                denoiserModule.getInputImageConfig("diffuseHitDepthImage"));

        connect(rayTracingModule.getOutputImageConfig("specular_hit_depth"),
                denoiserModule.getInputImageConfig("specularHitDepthImage"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_clear"),
                denoiserModule.getInputImageConfig("first_hit_clear"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_base_emission"),
                denoiserModule.getInputImageConfig("first_hit_base_emission"));

        connect(rayTracingModule.getOutputImageConfig("fog_image"),
                denoiserModule.getInputImageConfig("fog_image"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_refraction"),
                denoiserModule.getInputImageConfig("first_hit_refraction"));

        connect(denoiserModule.getOutputImageConfig("denoised_radiance"),
                upscalerModule.getInputImageConfig("color"));

        connect(rayTracingModule.getOutputImageConfig("linear_depth"),
                upscalerModule.getInputImageConfig("depth"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_depth"),
                upscalerModule.getInputImageConfig("first_hit_depth"));

        connect(rayTracingModule.getOutputImageConfig("motion_vector"),
                upscalerModule.getInputImageConfig("motion_vector"));
        connect(rayTracingModule.getOutputImageConfig("normal_roughness"),
                upscalerModule.getInputImageConfig("normal_roughness"));

        connect(upscalerModule.getOutputImageConfig("upscaled_radiance"),
                toneMappingModule.getInputImageConfig("denoised_radiance"));
        connect(upscalerModule.getOutputImageConfig("upscaled_radiance"),
                postRenderModule.getInputImageConfig("hdr_input"));
        connect(upscalerModule.getOutputImageConfig("upscaled_first_hit_depth"),
                postRenderModule.getInputImageConfig("first_hit_depth"));
        connect(upscalerModule.getOutputImageConfig("upscaled_motion_vector"),
                postRenderModule.getInputImageConfig("motion_vector"));
        connect(upscalerModule.getOutputImageConfig("upscaled_normal_roughness"),
                postRenderModule.getInputImageConfig("normal_roughness"));

        connect(toneMappingModule.getOutputImageConfig("mapped_output"),
                postRenderModule.getInputImageConfig("ldr_input"));

        connectOutput(postRenderModule.getOutputImageConfig("post_rendered"));
    }
    private static void assembleNRDInternal() {
        clear();

        Module rayTracingModule = addModule(RAY_TRACING_MODULE_NAME);

        Module denoiserModule = addModule(NRD_MODULE_NAME);

        Module temporalAccumulationModule = addModule(TEMPORAL_ACCUMULATION_MODULE_NAME);

        Module toneMappingModule = addModule(TONE_MAPPING_MODULE_NAME);

        Module postRenderModule = addModule(POST_RENDER_MODULE_NAME);

        rayTracingModule.x = 100;
        rayTracingModule.y = 220;
        denoiserModule.x = 380;
        denoiserModule.y = 120;
        temporalAccumulationModule.x = 660;
        temporalAccumulationModule.y = 120;
        toneMappingModule.x = 940;
        toneMappingModule.y = 120;
        postRenderModule.x = 940;
        postRenderModule.y = 300;

        INSTANCE.activePresetName = Presets.RT_NRD.key;

        connect(rayTracingModule.getOutputImageConfig("first_hit_diffuse_indirect_light"),
                denoiserModule.getInputImageConfig("diffuse_radiance"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_specular"),
                denoiserModule.getInputImageConfig("specular_radiance"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_diffuse_direct_light"),
                denoiserModule.getInputImageConfig("direct_radiance"));

        connect(rayTracingModule.getOutputImageConfig("diffuse_albedo_metallic"),
                denoiserModule.getInputImageConfig("diffuse_albedo"));

        connect(rayTracingModule.getOutputImageConfig("specular_albedo"),
                denoiserModule.getInputImageConfig("specular_albedo"));

        connect(rayTracingModule.getOutputImageConfig("normal_roughness"),
                denoiserModule.getInputImageConfig("normal_roughness"));

        connect(rayTracingModule.getOutputImageConfig("motion_vector"),
                denoiserModule.getInputImageConfig("motion_vector"));

        connect(rayTracingModule.getOutputImageConfig("linear_depth"),
                denoiserModule.getInputImageConfig("linear_depth"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_depth"),
                denoiserModule.getInputImageConfig("diffuseHitDepthImage"));

        connect(rayTracingModule.getOutputImageConfig("specular_hit_depth"),
                denoiserModule.getInputImageConfig("specularHitDepthImage"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_clear"),
                denoiserModule.getInputImageConfig("first_hit_clear"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_base_emission"),
                denoiserModule.getInputImageConfig("first_hit_base_emission"));

        connect(rayTracingModule.getOutputImageConfig("fog_image"),
                denoiserModule.getInputImageConfig("fog_image"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_refraction"),
                denoiserModule.getInputImageConfig("first_hit_refraction"));

        connect(denoiserModule.getOutputImageConfig("denoised_radiance"),
                temporalAccumulationModule.getInputImageConfig("color"));

        connect(rayTracingModule.getOutputImageConfig("motion_vector"),
                temporalAccumulationModule.getInputImageConfig("motion"));

        connect(rayTracingModule.getOutputImageConfig("normal_roughness"),
                temporalAccumulationModule.getInputImageConfig("normal_roughness"));

        connect(temporalAccumulationModule.getOutputImageConfig("accumulated_radiance"),
                toneMappingModule.getInputImageConfig("denoised_radiance"));
        connect(temporalAccumulationModule.getOutputImageConfig("accumulated_radiance"),
                postRenderModule.getInputImageConfig("hdr_input"));

        connect(rayTracingModule.getOutputImageConfig("first_hit_depth"),
                postRenderModule.getInputImageConfig("first_hit_depth"));
        connect(rayTracingModule.getOutputImageConfig("motion_vector"),
                postRenderModule.getInputImageConfig("motion_vector"));
        connect(rayTracingModule.getOutputImageConfig("normal_roughness"),
                postRenderModule.getInputImageConfig("normal_roughness"));

        connect(toneMappingModule.getOutputImageConfig("mapped_output"),
                postRenderModule.getInputImageConfig("ldr_input"));

        connectOutput(postRenderModule.getOutputImageConfig("post_rendered"));
    }

    static void assemblePresetByKeyInternal(String presetName) {
        if (Objects.equals(presetName, Presets.RT_DLSSRR.key)) {
            assembleDLSSRRInternal();
            return;
        }

        if (Objects.equals(presetName, Presets.RT_NRD.key)) {
            assembleNRDInternal();
            return;
        }

        if (Objects.equals(presetName, Presets.RT_NRD_FSR.key)) {
            assembleNRDFSRInternal();
            return;
        }

        if (Objects.equals(presetName, Presets.RT_NRD_XESS.key)) {
            assembleNRDXESSInternal();
            return;
        }

        if (Objects.equals(presetName, Presets.RT_NRD_DLSS.key)) {
            assembleNRDDLSSInternal();
            return;
        }

        throw new RuntimeException("Unsupported preset: " + presetName);
    }
}
