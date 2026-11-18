package com.g2806.radiante.client.proxy.vulkan;

/**
 * JNI bridge to the native Photo Mode reference accumulator and PNG / OpenEXR exporter.
 */
public final class PhotoProxy {

    private PhotoProxy() {
    }

    public static native void setActive(boolean active);

    public static native boolean isActive();

    public static native void setLens(float apertureRadius, float focusDistance, int blades, float bladeRotation);

    public static native void setTargetSamples(int targetSamples);

    /** The denoiser and upscaler run and their output is averaged (true), or the raw paths are (false). */
    public static native void setDenoise(boolean denoise);

    /** The frame a saved photo is cut to, width over height; 0 keeps the whole picture. */
    public static native void setCaptureAspect(float aspect);

    public static native int getTargetSamples();

    public static native int getSampleCount();

    public static native void resetAccumulation();

    public static native void setExposureBias(float exposureBias);

    /** Queues saving {@code pngPath} and/or {@code exrPath} (empty or null skips that file). */
    public static native void requestCapture(String pngPath, String exrPath, boolean waitForTarget);

    /**
     * Polls capture completion: 0 idle, 1 waiting for samples/GPU readback, 2 saved, -1 error.
     * Writes the status detail or file name into {@code outMessage[0]} when non-null.
     */
    public static native int pollCaptureStatus(String[] outMessage);
}
