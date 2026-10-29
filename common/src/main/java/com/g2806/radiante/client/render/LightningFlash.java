package com.g2806.radiante.client.render;

import com.g2806.radiante.mixin.world.ClientLevelAccessor;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LightningBolt;
import net.minecraft.world.phys.Vec3;
import org.joml.Vector4f;

/**
 * A lightning flash, for radiante-pt to light the world with while the sky flashes (celestial.glsl): its strength in
 * the z of the rain uniform, and the bearing of the nearest bolt from the camera in w. The game flashes the sky for two
 * ticks at a time, once to three times per bolt; the strength jumps to full with each and dies away over a tenth of a
 * second, so the light flickers with the bolt instead of switching. Nothing with the "Hide Lightning Flashes"
 * accessibility option, as the game's own flash.
 */
public final class LightningFlash {

    /** How fast a flash dies away once the sky stops flashing, in seconds. */
    private static final float FADE_SECONDS = 0.1f;

    private static float strength;
    private static float bearing;
    private static long lastTime = System.nanoTime();

    private LightningFlash() {
    }

    /** Sets z (strength 0..1) and w (bearing in radians, from +x towards +z) of the rain uniform. */
    public static Vector4f apply(Vector4f rain, Minecraft minecraft, Vec3 camera) {
        long now = System.nanoTime();
        float dt = Math.min((now - lastTime) * 1e-9f, 0.1f);
        lastTime = now;
        ClientLevel level = minecraft.level;
        if (level == null || minecraft.options.hideLightningFlash().get()) {
            strength = 0.0f;
        } else if (((ClientLevelAccessor) level).radiante$skyFlashTime() > 0) {
            strength = 1.0f;
            Entity nearest = null;
            double nearestDistance = Double.MAX_VALUE;
            for (Entity entity : level.entitiesForRendering()) {
                if (entity instanceof LightningBolt) {
                    double distance = entity.position().distanceToSqr(camera);
                    if (distance < nearestDistance) {
                        nearestDistance = distance;
                        nearest = entity;
                    }
                }
            }
            if (nearest != null) {
                double dx = nearest.getX() - camera.x;
                double dz = nearest.getZ() - camera.z;
                if (dx * dx + dz * dz > 1e-4) {
                    bearing = (float) Math.atan2(dz, dx);
                }
            }
        } else if (!minecraft.isPaused()) {
            strength *= (float) Math.exp(-dt / FADE_SECONDS);
            if (strength < 0.002f) {
                strength = 0.0f;
            }
        }
        return rain.setComponent(2, strength).setComponent(3, bearing);
    }
}
