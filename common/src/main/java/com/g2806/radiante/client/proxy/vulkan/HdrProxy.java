package com.g2806.radiante.client.proxy.vulkan;

/** The native HDR display output (core/render/hdr_output.cpp). */
public final class HdrProxy {

    private HdrProxy() {
    }

    /**
     * Records the HDR composition of Minecraft's final image into the swapchain image on Minecraft's present command
     * buffer, in place of its blit. The swapchain image must be in TRANSFER_DST_OPTIMAL. Returns false when it could
     * not, and the caller blits as usual. {@code flipY}: Minecraft's own blit turns the image upside down (its
     * render targets are stored bottom row first), and the composition has to present it the same way.
     * {@code debugView} tints blue everything shown as SDR, leaving what is shown in HDR as it is.
     */
    public static native boolean compose(long commandBuffer, long minecraftImage, int minecraftLayout,
        int minecraftFormat, int width, int height, long swapchainImage, int swapchainWidth, int swapchainHeight,
        float paperWhiteNits, float peakNits, boolean flipY, boolean debugView);

    /**
     * The HDR calibration screen's pictures for the following compositions, in fractions of the image, top down:
     * mode 1 is the peak pattern in the first rectangle, mode 2 Minecraft's image with its highlights brought back in
     * both, and mode 0 ends it.
     */
    public static native void setCalibration(int mode, float ax, float ay, float aw, float ah, float bx, float by,
        float bw, float bh);
}
