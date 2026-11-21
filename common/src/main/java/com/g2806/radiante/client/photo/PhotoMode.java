package com.g2806.radiante.client.photo;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.gui.PhotoModeScreen;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.PipelineSettings;
import com.g2806.radiante.client.proxy.vulkan.PhotoProxy;
import com.g2806.radiante.client.render.ChunkManager;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.g2806.radiante.compat.VersionCompat;
import com.mojang.blaze3d.platform.InputConstants;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Optional;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.util.Mth;
import net.minecraft.util.Util;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.joml.Vector4f;

/**
 * Client-side controller for Photo Mode: free-fly camera pose, FOV, camera roll, physical thin-lens aperture
 * (radius, focus distance, polygonal blades and blade angle), live tone-mapped exposure bias, click-to-focus
 * raycasting, and progressive reference PNG / OpenEXR screenshot capture.
 */
public final class PhotoMode {

    public static final int[] TARGET_SAMPLE_PRESETS = {64, 256, 1024, 4096};
    public static final int[] BLADE_PRESETS = {0, 5, 6, 7, 8};

    private static final DateTimeFormatter FILE_TIMESTAMP = DateTimeFormatter.ofPattern("yyyy-MM-dd_HH.mm.ss");
    private static final String EXPOSURE_BIAS_ATTR = "render_pipeline.module.tone_mapping.attribute.exposure_bias";

    private static volatile boolean active;
    private static boolean needsInitialSync;
    private static Object activeLevel;
    private static int activeChunkGeneration;

    private static Vec3 cameraPos = Vec3.ZERO;
    private static Vec3 initialPos = Vec3.ZERO;
    private static float yaw;
    private static float pitch;
    private static float initialYaw;
    private static float initialPitch;
    private static float rollDegrees;

    private static float fovDegrees = 70.0f;
    private static float initialFovDegrees = 70.0f;
    private static float apertureRadius = 0.05f;
    private static float focusDistance = 10.0f;
    private static int blades = 0;
    private static float bladeRotationDegrees = 0.0f;
    private static float exposureBias = 0.0f;
    private static float initialExposureBias = 0.0f;
    private static int targetSamples = 256;
    private static boolean hideHands = true;
    /**
     * The denoiser in Photo Mode, on by default: averaging the raw paths needs thousands of samples before the noise
     * goes, and at the default 256 the picture still looked like the game with its denoiser off.
     */
    private static boolean denoise = true;
    private static boolean savedHideGui = false;
    private static float moveSpeed = 6.0f;

    private static long lastStepNanos;
    private static long frozenMillis;
    private static float frozenDayFraction = -1.0f;
    private static float frozenWindTime = -1.0f;
    private static float frozenSunAngle = Float.NaN;

    private static final String[] pollMessageSlot = new String[1];
    private static int lastCaptureStatus;
    private static String statusBanner = "";
    private static long statusBannerUntilMillis;

    private PhotoMode() {
    }

    public static boolean isActive() {
        return active;
    }

    /** Enters Photo Mode when ray tracing is running in a loaded world. */
    public static boolean enter(Minecraft minecraft) {
        if (!RadianteRenderer.isRayTracingEnabled() || minecraft.level == null || minecraft.player == null) {
            return false;
        }
        if (active) {
            return true;
        }
        initialFovDegrees = minecraft.options.fov().get();
        fovDegrees = initialFovDegrees;
        rollDegrees = 0.0f;
        apertureRadius = 0.05f;
        blades = 0;
        bladeRotationDegrees = 0.0f;
        hideHands = true;
        targetSamples = 256;

        initialExposureBias = readStoredExposureBias();
        exposureBias = initialExposureBias;

        cameraPos = minecraft.player.getEyePosition(1.0f);
        initialPos = cameraPos;
        yaw = minecraft.player.getYRot();
        pitch = minecraft.player.getXRot();
        initialYaw = yaw;
        initialPitch = pitch;
        focusDistance = measureRayDistance(minecraft, cameraPos, forwardVector(yaw, pitch), 128.0);

        needsInitialSync = true;
        activeLevel = minecraft.level;
        activeChunkGeneration = ChunkManager.generation();
        lastStepNanos = System.nanoTime();
        frozenMillis = Util.getMillis();
        frozenDayFraction = -1.0f;
        frozenWindTime = -1.0f;
        frozenSunAngle = Float.NaN;
        statusBanner = "";
        statusBannerUntilMillis = 0L;
        lastCaptureStatus = 0;

        savedHideGui = minecraft.gui.hud.isHidden();
        if (!savedHideGui) {
            minecraft.gui.hud.toggle();
        }

        active = true;
        PhotoProxy.setTargetSamples(targetSamples);
        PhotoProxy.setDenoise(denoise);
        PhotoCinema.push();
        PhotoProxy.setActive(true);
        pushLens();
        PhotoProxy.setExposureBias(exposureBias);
        return true;
    }

    /** Leaves Photo Mode and restores the player's normal rendering pipeline. */
    public static void exit() {
        if (!active) {
            return;
        }
        active = false;
        Minecraft minecraft = Minecraft.getInstance();
        if (minecraft != null && minecraft.gui != null && minecraft.gui.hud != null
            && minecraft.gui.hud.isHidden() != savedHideGui) {
            minecraft.gui.hud.toggle();
        }
        needsInitialSync = false;
        activeLevel = null;
        frozenDayFraction = -1.0f;
        frozenWindTime = -1.0f;
        frozenSunAngle = Float.NaN;
        statusBanner = "";
        PhotoProxy.setActive(false);
        // The photo's own time and weather end with it: the world's are read again from the next frame.
        boolean sceneChanged = PhotoCinema.sceneChanged();
        PhotoCinema.clearScene();
        if (sceneChanged && minecraft != null && minecraft.level != null) {
            minecraft.level.environmentAttributes().invalidateTickCache();
        }
    }

    /** Exits when leaving a world, and restarts progressive accumulation when switching worlds or reloading chunks. */
    public static void checkWorld(Minecraft minecraft) {
        if (!active) {
            return;
        }
        if (minecraft == null || minecraft.level == null || minecraft.player == null
            || !RadianteRenderer.isRayTracingEnabled()) {
            exit();
            if (minecraft != null && minecraft.gui != null && minecraft.gui.screen() instanceof PhotoModeScreen) {
                minecraft.gui.setScreen(null);
            }
            return;
        }
        if (minecraft.level != activeLevel) {
            activeLevel = minecraft.level;
            activeChunkGeneration = ChunkManager.generation();
            cameraPos = minecraft.player.getEyePosition(1.0f);
            initialPos = cameraPos;
            yaw = minecraft.player.getYRot();
            pitch = minecraft.player.getXRot();
            initialYaw = yaw;
            initialPitch = pitch;
            needsInitialSync = true;
            frozenMillis = Util.getMillis();
            frozenDayFraction = -1.0f;
            frozenWindTime = -1.0f;
            frozenSunAngle = Float.NaN;
            PhotoProxy.resetAccumulation();
            return;
        }
        int generation = ChunkManager.generation();
        if (generation != activeChunkGeneration) {
            activeChunkGeneration = generation;
            PhotoProxy.resetAccumulation();
        }
    }

    /** Resets camera pose, FOV, roll, lens and exposure back to how Photo Mode opened. */
    public static void resetParameters(Minecraft minecraft) {
        cameraPos = initialPos;
        yaw = initialYaw;
        pitch = initialPitch;
        rollDegrees = 0.0f;
        fovDegrees = initialFovDegrees;
        apertureRadius = 0.05f;
        blades = 0;
        bladeRotationDegrees = 0.0f;
        exposureBias = initialExposureBias;
        hideHands = true;
        if (minecraft != null && minecraft.level != null) {
            focusDistance = measureRayDistance(minecraft, cameraPos, forwardVector(yaw, pitch), 128.0);
        } else {
            focusDistance = 10.0f;
        }
        pushLens();
        PhotoProxy.setExposureBias(exposureBias);
        PhotoProxy.resetAccumulation();
    }

    /**
     * Captures the exact render-state camera on the first frame after opening (including third-person offsets) and
     * advances smooth free-fly WASD / Space / Shift movement while keys are held.
     */
    public static void stepFrame(Minecraft minecraft, CameraRenderState cameraState, boolean allowKeyboardMove) {
        checkWorld(minecraft);
        if (!active) {
            return;
        }
        if (needsInitialSync) {
            needsInitialSync = false;
            cameraPos = cameraState.pos;
            initialPos = cameraPos;
            Vector3f fwd = new Matrix4f(cameraState.viewRotationMatrix).transpose()
                .transformDirection(new Vector3f(0.0f, 0.0f, -1.0f)).normalize();
            pitch = (float) Math.toDegrees(Math.asin(Math.clamp(-fwd.y, -1.0f, 1.0f)));
            yaw = (float) Math.toDegrees(Math.atan2(-fwd.x, fwd.z));
            initialYaw = yaw;
            initialPitch = pitch;
            if (minecraft.level != null) {
                focusDistance = measureRayDistance(minecraft, cameraPos, new Vec3(fwd.x, fwd.y, fwd.z), 128.0);
                pushLens();
            }
            lastStepNanos = System.nanoTime();
            return;
        }

        long now = System.nanoTime();
        float dt = Math.clamp((now - lastStepNanos) * 1.0e-9f, 0.0f, 0.1f);
        lastStepNanos = now;
        if (!allowKeyboardMove || dt <= 0.0f) {
            return;
        }

        float forwardInput = 0.0f;
        float strafeInput = 0.0f;
        float verticalInput = 0.0f;
        if (VersionCompat.isKeyDown(InputConstants.KEY_W)) forwardInput += 1.0f;
        if (VersionCompat.isKeyDown(InputConstants.KEY_S)) forwardInput -= 1.0f;
        if (VersionCompat.isKeyDown(InputConstants.KEY_D)) strafeInput += 1.0f;
        if (VersionCompat.isKeyDown(InputConstants.KEY_A)) strafeInput -= 1.0f;
        if (VersionCompat.isKeyDown(InputConstants.KEY_SPACE) || VersionCompat.isKeyDown(InputConstants.KEY_E)) {
            verticalInput += 1.0f;
        }
        if (VersionCompat.isKeyDown(InputConstants.KEY_LSHIFT) || VersionCompat.isKeyDown(InputConstants.KEY_Q)) {
            verticalInput -= 1.0f;
        }

        if (forwardInput == 0.0f && strafeInput == 0.0f && verticalInput == 0.0f) {
            return;
        }

        float boost = VersionCompat.isKeyDown(InputConstants.KEY_LCONTROL) ? 3.0f : 1.0f;
        double step = moveSpeed * boost * dt;
        Vec3 fwd = forwardVector(yaw, pitch);
        double yawRad = Math.toRadians(yaw);
        // Right vector on the horizontal plane in Minecraft's world coordinates.
        Vec3 right = new Vec3(-Math.cos(yawRad), 0.0, -Math.sin(yawRad));
        Vec3 delta = fwd.scale(forwardInput).add(right.scale(strafeInput)).add(0.0, verticalInput, 0.0);
        if (delta.lengthSqr() > 1.0e-8) {
            cameraPos = cameraPos.add(delta.normalize().scale(step));
        }
    }

    public static void rotateCamera(float deltaYawDegrees, float deltaPitchDegrees) {
        if (!active) {
            return;
        }
        yaw = Mth.wrapDegrees(yaw + deltaYawDegrees);
        pitch = Math.clamp(pitch + deltaPitchDegrees, -89.9f, 89.9f);
    }

    public static void setCameraPose(double x, double y, double z, float newYaw, float newPitch, float newRoll) {
        needsInitialSync = false;
        cameraPos = new Vec3(x, y, z);
        yaw = newYaw;
        pitch = Math.clamp(newPitch, -89.9f, 89.9f);
        rollDegrees = Math.clamp(newRoll, -90.0f, 90.0f);
        PhotoProxy.resetAccumulation();
    }

    public static Vec3 cameraPos() {
        return cameraPos;
    }

    /** World-to-camera rotation matrix (matching {@code CameraRenderState.viewRotationMatrix}), including roll. */
    public static Matrix4f viewRotationMatrix() {
        return new Matrix4f()
            .rotateZ((float) Math.toRadians(rollDegrees))
            .rotateX((float) Math.toRadians(pitch))
            .rotateY((float) Math.toRadians(yaw - 180.0f));
    }

    /** Perspective projection matrix preserving RenderPearl's clip-space conventions at Photo Mode's FOV. */
    public static Matrix4f projectionMatrix(Matrix4f baseProjection, int width, int height) {
        Matrix4f proj = new Matrix4f(baseProjection);
        float signX = baseProjection.m00() < 0.0f ? -1.0f : 1.0f;
        float signY = baseProjection.m11() < 0.0f ? -1.0f : 1.0f;
        float tanHalfFov = (float) Math.tan(Math.toRadians(Math.clamp(fovDegrees, 10.0f, 130.0f)) * 0.5);
        float absM11 = 1.0f / Math.max(tanHalfFov, 1.0e-4f);
        float aspect = (float) Math.max(1, width) / (float) Math.max(1, height);
        proj.m00(signX * (absM11 / aspect));
        proj.m11(signY * absM11);
        proj.m20(0.0f);
        proj.m21(0.0f);
        return proj;
    }

    /** Focuses the thin-lens focal plane on the block or entity under the given GUI screen coordinates. */
    public static boolean focusAtScreenPoint(Minecraft minecraft, double guiX, double guiY, int guiWidth, int guiHeight) {
        if (!active || minecraft.level == null || guiWidth <= 0 || guiHeight <= 0) {
            return false;
        }
        float ndcX = (float) (guiX / guiWidth * 2.0 - 1.0);
        // Screen Y runs top-to-bottom; view space +Y is up.
        float ndcY = (float) (1.0 - guiY / guiHeight * 2.0);
        float tanHalfFov = (float) Math.tan(Math.toRadians(Math.clamp(fovDegrees, 10.0f, 130.0f)) * 0.5);
        float aspect = (float) guiWidth / (float) guiHeight;

        Vector3f dirView = new Vector3f(ndcX * aspect * tanHalfFov, ndcY * tanHalfFov, -1.0f).normalize();
        Vector3f dirWorld = viewRotationMatrix().transpose().transformDirection(dirView).normalize();
        Vec3 rayDir = new Vec3(dirWorld.x, dirWorld.y, dirWorld.z);

        float hitDist = measureRayDistance(minecraft, cameraPos, rayDir, 256.0);
        Vec3 hitPoint = cameraPos.add(rayDir.scale(hitDist));
        Vec3 opticalAxis = forwardVector(yaw, pitch);
        float perpFocus = (float) Math.max(0.25, hitPoint.subtract(cameraPos).dot(opticalAxis));
        setFocusDistance(perpFocus);
        return true;
    }

    private static float measureRayDistance(Minecraft minecraft, Vec3 origin, Vec3 rayDir, double maxReach) {
        if (minecraft.level == null || minecraft.player == null) {
            return 10.0f;
        }
        Vec3 end = origin.add(rayDir.scale(maxReach));
        BlockHitResult blockHit = minecraft.level.clip(
            new ClipContext(origin, end, ClipContext.Block.VISUAL, ClipContext.Fluid.ANY, minecraft.player));
        double closest = blockHit.getType() != HitResult.Type.MISS
            ? origin.distanceTo(blockHit.getLocation()) : maxReach;

        AABB searchBox = new AABB(origin, origin.add(rayDir.scale(closest))).inflate(1.0);
        for (Entity entity : minecraft.level.getEntities(minecraft.player, searchBox, e -> !e.isSpectator() && e.isPickable())) {
            AABB box = entity.getBoundingBox().inflate(entity.getPickRadius());
            Optional<Vec3> clip = box.clip(origin, end);
            if (clip.isPresent()) {
                double d = origin.distanceTo(clip.get());
                if (d < closest) {
                    closest = d;
                }
            }
        }
        if (closest >= maxReach - 1.0e-3) {
            return focusDistance;
        }
        return (float) Math.clamp(closest, 0.25, 256.0);
    }

    private static Vec3 forwardVector(float yawDeg, float pitchDeg) {
        float yawRad = (float) Math.toRadians(yawDeg);
        float pitchRad = (float) Math.toRadians(pitchDeg);
        float cosPitch = Mth.cos(pitchRad);
        return new Vec3(-Mth.sin(yawRad) * cosPitch, -Mth.sin(pitchRad), Mth.cos(yawRad) * cosPitch);
    }

    // ---- Getters & setters ----

    public static float apertureRadius() {
        return apertureRadius;
    }

    public static void setApertureRadius(float radius) {
        float clamped = Math.clamp(radius, 0.0f, 0.5f);
        if (Math.abs(clamped - apertureRadius) > 1.0e-5f) {
            apertureRadius = clamped;
            pushLens();
        }
    }

    public static float focusDistance() {
        return focusDistance;
    }

    public static void setFocusDistance(float distance) {
        float clamped = Math.clamp(distance, 0.25f, 256.0f);
        if (Math.abs(clamped - focusDistance) > 1.0e-4f) {
            focusDistance = clamped;
            pushLens();
        }
    }

    public static int blades() {
        return blades;
    }

    public static void setBlades(int bladeCount) {
        int next = bladeCount < 3 ? 0 : Math.min(bladeCount, 16);
        if (next != blades) {
            blades = next;
            pushLens();
        }
    }

    public static void cycleBlades() {
        int idx = 0;
        for (int i = 0; i < BLADE_PRESETS.length; i++) {
            if (BLADE_PRESETS[i] == blades) {
                idx = (i + 1) % BLADE_PRESETS.length;
                break;
            }
        }
        setBlades(BLADE_PRESETS[idx]);
    }

    public static float bladeRotationDegrees() {
        return bladeRotationDegrees;
    }

    public static void setBladeRotationDegrees(float degrees) {
        float clamped = Math.clamp(degrees, 0.0f, 180.0f);
        if (Math.abs(clamped - bladeRotationDegrees) > 1.0e-4f) {
            bladeRotationDegrees = clamped;
            pushLens();
        }
    }

    public static float fovDegrees() {
        return fovDegrees;
    }

    public static void setFovDegrees(float degrees) {
        fovDegrees = Math.clamp(degrees, 15.0f, 120.0f);
    }

    public static float rollDegrees() {
        return rollDegrees;
    }

    public static void setRollDegrees(float degrees) {
        rollDegrees = Math.clamp(degrees, -45.0f, 45.0f);
    }

    public static float exposureBias() {
        return exposureBias;
    }

    public static void setExposureBias(float bias) {
        float clamped = Math.clamp(bias, -4.0f, 4.0f);
        if (Math.abs(clamped - exposureBias) > 1.0e-4f) {
            exposureBias = clamped;
            if (active) {
                PhotoProxy.setExposureBias(exposureBias);
            }
        }
    }

    public static int targetSamples() {
        return targetSamples;
    }

    public static boolean denoise() {
        return denoise;
    }

    public static void setDenoise(boolean enabled) {
        denoise = enabled;
        if (active) {
            PhotoProxy.setDenoise(denoise);
        }
    }

    public static void setTargetSamples(int samples) {
        targetSamples = Math.clamp(samples, 1, 16384);
        if (active) {
            PhotoProxy.setTargetSamples(targetSamples);
        }
    }

    public static void cycleTargetSamples() {
        int idx = 0;
        for (int i = 0; i < TARGET_SAMPLE_PRESETS.length; i++) {
            if (TARGET_SAMPLE_PRESETS[i] == targetSamples) {
                idx = (i + 1) % TARGET_SAMPLE_PRESETS.length;
                break;
            }
        }
        setTargetSamples(TARGET_SAMPLE_PRESETS[idx]);
    }

    public static int sampleCount() {
        return active ? PhotoProxy.getSampleCount() : 0;
    }

    public static boolean hideHands() {
        return hideHands;
    }

    public static void setHideHands(boolean hide) {
        if (hideHands != hide) {
            hideHands = hide;
            if (active) {
                PhotoProxy.resetAccumulation();
            }
        }
    }

    public static float moveSpeed() {
        return moveSpeed;
    }

    public static void adjustMoveSpeed(float scrollDelta) {
        moveSpeed = Math.clamp(moveSpeed * (scrollDelta > 0 ? 1.2f : 1.0f / 1.2f), 0.5f, 48.0f);
    }

    public static long frozenMillis() {
        return frozenMillis;
    }

    public static float frozenDayFraction(float liveDayFraction) {
        if (frozenDayFraction < 0.0f || isFollowingSky()) {
            frozenDayFraction = liveDayFraction;
        }
        return frozenDayFraction;
    }

    public static float frozenWindTime(float liveWindTime) {
        if (frozenWindTime < 0.0f) {
            frozenWindTime = liveWindTime;
        }
        return frozenWindTime;
    }

    /**
     * Lets the frozen sky take the world's time again (Photo Mode's time of day and weather): it is caught anew on
     * the next frame, and the picture starts over from it.
     */
    static void releaseFrozenSky(long millis) {
        // The sky and the rain move with the game's ticks, which a paused world does not run: for this long the
        // world runs on (isFollowingSky) and the sky follows it, then it is caught again where it got to.
        followSkyUntil = Math.max(followSkyUntil, Util.getMillis() + millis);
        if (active) {
            PhotoProxy.resetAccumulation();
        }
    }

    private static long followSkyUntil;

    /** True while the world runs on after a change of time or weather, so Photo Mode does not pause it. */
    public static boolean isFollowingSky() {
        return active && Util.getMillis() < followSkyUntil;
    }

    public static float frozenSunAngle(float liveSunAngle) {
        if (Float.isNaN(frozenSunAngle) || isFollowingSky()) {
            frozenSunAngle = liveSunAngle;
        }
        return frozenSunAngle;
    }

    private static void pushLens() {
        if (active) {
            PhotoProxy.setLens(apertureRadius, focusDistance, blades, (float) Math.toRadians(bladeRotationDegrees));
        }
    }

    private static float readStoredExposureBias() {
        String raw = PipelineSettings.getModuleValue(Pipeline.TONE_MAPPING_MODULE_NAME, EXPOSURE_BIAS_ATTR);
        if (raw != null) {
            try {
                return Math.clamp(Float.parseFloat(raw.trim()), -4.0f, 4.0f);
            } catch (NumberFormatException ignored) {
            }
        }
        return 0.0f;
    }

    // ---- Saving PNG / EXR ----

    public static void requestSave(Minecraft minecraft, boolean savePng, boolean saveExr, boolean waitForTarget) {
        String stamp = "radiante_photo_" + LocalDateTime.now().format(FILE_TIMESTAMP);
        requestSaveNamed(minecraft, stamp, savePng, saveExr, waitForTarget, true);
    }

    public static void requestSaveNamed(Minecraft minecraft, String baseName, boolean savePng, boolean saveExr,
        boolean waitForTarget, boolean avoidOverwrite) {
        if (!active || (!savePng && !saveExr)) {
            return;
        }
        Path dir = minecraft.gameDirectory.toPath().resolve("screenshots");
        try {
            Files.createDirectories(dir);
        } catch (IOException e) {
            RadianteClient.LOGGER.warn("Could not create screenshots directory {}", dir, e);
        }

        String stem = baseName;
        if (avoidOverwrite) {
            int suffix = 1;
            while ((savePng && Files.exists(dir.resolve(stem + ".png")))
                || (saveExr && Files.exists(dir.resolve(stem + ".exr")))) {
                suffix++;
                stem = baseName + "_" + suffix;
            }
        }

        String pngPath = savePng ? dir.resolve(stem + ".png").toAbsolutePath().toString() : "";
        String exrPath = saveExr ? dir.resolve(stem + ".exr").toAbsolutePath().toString() : "";
        PhotoProxy.requestCapture(pngPath, exrPath, waitForTarget);
        pendingPngPath = pngPath;
        lastCaptureStatus = 1;
    }

    /** The PNG the renderer is writing, for the logo drawn over it once it is there. */
    private static String pendingPngPath = "";

    /** Polls native capture state and updates the UI status banner. */
    public static void pollStatus() {
        if (!active) {
            return;
        }
        pollMessageSlot[0] = null;
        int status = PhotoProxy.pollCaptureStatus(pollMessageSlot);
        if (status == 1) {
            lastCaptureStatus = 1;
        } else if (status == 2) {
            lastCaptureStatus = 0;
            // The file is written: the logo goes over it, if one is chosen (PhotoWatermark).
            PhotoWatermark.applyLater(pendingPngPath);
            pendingPngPath = "";
            statusBanner = pollMessageSlot[0] != null ? pollMessageSlot[0] : "";
            statusBannerUntilMillis = Util.getMillis() + 5000L;
        } else if (status < 0) {
            lastCaptureStatus = -1;
            statusBanner = pollMessageSlot[0] != null ? pollMessageSlot[0] : "error";
            statusBannerUntilMillis = Util.getMillis() + 6000L;
        }
    }

    public static boolean isCapturePending() {
        return lastCaptureStatus == 1;
    }

    public static boolean didLastCaptureFail() {
        return lastCaptureStatus < 0 && Util.getMillis() < statusBannerUntilMillis;
    }

    public static String activeBannerText() {
        if (Util.getMillis() < statusBannerUntilMillis && !statusBanner.isEmpty()) {
            return statusBanner;
        }
        return "";
    }
}
