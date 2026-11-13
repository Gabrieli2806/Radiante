package com.g2806.radiante.client.render;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

/** The mip levels of the PBR atlases: averaged where that means something, kept where a value is a marker or code. */
class PbrMipsTest {

    private static int rgba(int r, int g, int b, int a) {
        return a << 24 | b << 16 | g << 8 | r;
    }

    @Test
    void normalsAverageToTheirMeanDirection() {
        // Two texels leaning right, two leaning left: the mean faces straight out.
        int right = rgba(218, 128, 200, 100);
        int left = rgba(37, 128, 100, 200);
        int mean = PbrAtlases.averageNormal(new int[] {right, left, right, left});
        assertTrue(Math.abs((mean & 0xFF) - 128) <= 1, "red " + (mean & 0xFF));
        assertTrue(Math.abs((mean >> 8 & 0xFF) - 128) <= 1, "green " + (mean >> 8 & 0xFF));
        assertEquals(150, mean >> 16 & 0xFF);
        assertEquals(150, mean >>> 24);
    }

    @Test
    void aTextureWithoutANormalMapKeepsItsMarker() {
        int none = PbrAtlases.NORMAL_DEFAULT;
        int flat = rgba(128, 128, 255, 255);
        assertEquals(none, PbrAtlases.averageNormal(new int[] {none, none, none, none}));
        assertEquals(flat, PbrAtlases.averageNormal(new int[] {flat, flat, flat, flat}));
    }

    @Test
    void specularAveragesRoughnessAndKeepsTheBrightestGlow() {
        int smooth = rgba(255, 10, 0, 255);
        int rough = rgba(0, 10, 0, 100);
        int mean = PbrAtlases.averageSpecular(new int[] {smooth, rough, smooth, rough}, smooth);
        // Roughness 0 and 1, squared and averaged: sqrt(0.5) = 0.71, smoothness 0.29.
        assertTrue(Math.abs((mean & 0xFF) - 75) <= 1, "smoothness " + (mean & 0xFF));
        assertEquals(10, mean >> 8 & 0xFF);
        assertEquals(100, mean >>> 24);
    }

    @Test
    void metalCodesAndMissingMapsAreNotBlended() {
        int iron = rgba(200, 230, 0, 255);
        int stone = rgba(100, 10, 0, 255);
        assertEquals(230, PbrAtlases.averageSpecular(new int[] {iron, stone, stone, stone}, iron) >> 8 & 0xFF);
        assertEquals(0, PbrAtlases.averageSpecular(new int[] {0, 0, 0, 0}, 0));
    }
}
