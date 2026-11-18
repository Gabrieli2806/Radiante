package com.g2806.radiante.client.gui;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.g2806.radiante.client.pipeline.Presets;
import com.g2806.radiante.client.render.TracedTextureDetail;
import org.junit.jupiter.api.Test;

class AutoTuneTest {

    @Test
    void picksVendorOptimalPreset() {
        AutoTune.Result nvidiaRr = AutoTune.recommend(new AutoTune.Probe(
            "NVIDIA GeForce RTX 4070", true, false, true, true, false, true, true,
            1920, 1080, 1800, 11200, 12288, 6.2f, 1.0));
        assertEquals(Presets.RT_DLSSRR, nvidiaRr.preset());
        assertTrue(nvidiaRr.rayReconstruction());
        assertTrue(nvidiaRr.reflex());

        AutoTune.Result nvidiaSrOnly = AutoTune.recommend(new AutoTune.Probe(
            "NVIDIA GeForce RTX 2060", true, false, false, true, false, true, false,
            1920, 1080, 1500, 5200, 6144, 15.0f, 1.0));
        assertEquals(Presets.RT_NRD_DLSS, nvidiaSrOnly.preset());
        assertFalse(nvidiaSrOnly.rayReconstruction());

        AutoTune.Result intelXess = AutoTune.recommend(new AutoTune.Probe(
            "Intel(R) Arc(TM) B580 Graphics", false, true, false, false, true, true, false,
            1920, 1080, 1600, 11000, 12288, 10.5f, 1.0));
        assertEquals(Presets.RT_NRD_XESS, intelXess.preset());

        AutoTune.Result amdFsr = AutoTune.recommend(new AutoTune.Probe(
            "AMD Radeon RX 7800 XT", false, false, false, false, true, true, false,
            2560, 1440, 2100, 15000, 16384, 9.8f, 1.0));
        assertEquals(Presets.RT_NRD_FSR, amdFsr.preset());
    }

    @Test
    void loadFactorNormalizationPreventsOscillation() {
        // Same GPU whose true Balanced+High cost is 10.0 ms:
        // When measured on LOW (loadFactor = 0.42 -> 4.2 ms) vs on ULTRA (loadFactor = 1.65 -> 16.5 ms),
        // both normalize to ~10.0 ms and choose HIGH without oscillating.
        AutoTune.Result fromLow = AutoTune.recommend(new AutoTune.Probe(
            "NVIDIA GeForce RTX 4060 Ti", true, false, true, true, false, true, true,
            1920, 1080, 1400, 14000, 16384, 4.2f, 0.42));
        AutoTune.Result fromUltra = AutoTune.recommend(new AutoTune.Probe(
            "NVIDIA GeForce RTX 4060 Ti", true, false, true, true, false, true, true,
            1920, 1080, 2200, 14000, 16384, 16.5f, 1.65));
        assertEquals(QualityPreset.HIGH, fromLow.quality());
        assertEquals(QualityPreset.HIGH, fromUltra.quality());
    }

    @Test
    void clampsQualityAndTexturesUnderTightVramBudget() {
        // Fast GPU time (6.0 ms) but tight 4.8 GB VRAM budget left by other apps: must clamp to MEDIUM, <= 10 chunks, HALF textures.
        AutoTune.Result tightVram = AutoTune.recommend(new AutoTune.Probe(
            "NVIDIA GeForce RTX 3070", true, false, true, true, false, true, true,
            1920, 1080, 2400, 4800, 8192, 6.0f, 1.0));
        assertEquals(QualityPreset.MEDIUM, tightVram.quality());
        assertTrue(tightVram.renderDistance() <= 10);
        assertEquals(TracedTextureDetail.HALF, tightVram.tracedTextureDetail());

        // 8 GB card with 7.8 GB budget: caps ULTRA down to HIGH and render distance <= 14.
        AutoTune.Result eightGb = AutoTune.recommend(new AutoTune.Probe(
            "NVIDIA GeForce RTX 4070 Laptop", true, false, true, true, false, true, true,
            1920, 1080, 1800, 7800, 8192, 5.5f, 1.0));
        assertEquals(QualityPreset.HIGH, eightGb.quality());
        assertTrue(eightGb.renderDistance() <= 14);
        assertEquals(TracedTextureDetail.AUTO, eightGb.tracedTextureDetail());
    }

    @Test
    void highEndGpuWithAmpleVramPicksUltra() {
        AutoTune.Result ultra = AutoTune.recommend(new AutoTune.Probe(
            "NVIDIA GeForce RTX 4090", true, false, true, true, false, true, true,
            2560, 1440, 2500, 22000, 24576, 5.1f, 1.0));
        assertEquals(QualityPreset.ULTRA, ultra.quality());
        assertEquals(24, ultra.renderDistance());
        assertEquals(TracedTextureDetail.AUTO, ultra.tracedTextureDetail());
    }

    @Test
    void menuEstimateParsesSummaryAndScalesWithResolution() {
        assertEquals(8.42f, AutoTune.parseSummaryMs("8.42 ms (world 6.91, DLSS 0.72)"), 1e-3f);
        assertEquals(0.0f, AutoTune.parseSummaryMs(""), 1e-3f);

        float ms1080 = AutoTune.estimateNormalizedMsFromHardware(
            "NVIDIA GeForce RTX 4070", true, false, 1920, 1080, 12288, 11500);
        float ms4k = AutoTune.estimateNormalizedMsFromHardware(
            "NVIDIA GeForce RTX 4070", true, false, 3840, 2160, 12288, 11500);
        assertTrue(ms4k > ms1080 * 2.5f);
    }
}
