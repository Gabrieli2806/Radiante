package com.g2806.radiante.client.compat.distanthorizons;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class LodTerrainScalingTest {

    @Test
    void builderThreadCountScalesWithProcessorsAndStaysClamped() {
        assertEquals(2, LodTerrain.builderThreadCount(1));
        assertEquals(2, LodTerrain.builderThreadCount(4));
        assertEquals(2, LodTerrain.builderThreadCount(6));
        assertEquals(4, LodTerrain.builderThreadCount(12));
        assertEquals(5, LodTerrain.builderThreadCount(16));
        assertEquals(8, LodTerrain.builderThreadCount(24));
        assertEquals(8, LodTerrain.builderThreadCount(64));
    }

    @Test
    void squareMarginChunksWidensWithHorizontalSpeed() {
        assertEquals(4, LodTerrain.squareMarginChunks(Double.NaN));
        assertEquals(4, LodTerrain.squareMarginChunks(0.0));
        assertEquals(4, LodTerrain.squareMarginChunks(5.5));
        assertTrue(LodTerrain.squareMarginChunks(22.0) > 4);
        assertTrue(LodTerrain.squareMarginChunks(40.0) > LodTerrain.squareMarginChunks(22.0));
        assertEquals(12, LodTerrain.squareMarginChunks(120.0));
    }

    @Test
    void reselectIntervalShortensWhenFlyingFast() {
        long idle = LodTerrain.reselectIntervalNs(0.0);
        long walk = LodTerrain.reselectIntervalNs(5.0);
        long creative = LodTerrain.reselectIntervalNs(22.0);
        long elytra = LodTerrain.reselectIntervalNs(50.0);

        assertEquals(500_000_000L, idle);
        assertEquals(idle, walk);
        assertTrue(creative < walk);
        assertEquals(150_000_000L, elytra);
    }
}
