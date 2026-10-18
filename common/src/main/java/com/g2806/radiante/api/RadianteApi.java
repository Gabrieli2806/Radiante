package com.g2806.radiante.api;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.render.RadianteRenderer;
import net.minecraft.world.phys.Vec3;
import org.joml.Matrix4f;
import org.joml.Vector3f;

/**
 * What other mods may rely on. Everything else in Radiante is internal and can change between versions.
 */
public final class RadianteApi {

    private RadianteApi() {
    }

    /** Whether the world is being ray traced right now, rather than drawn by Minecraft. */
    public static boolean isRayTracingEnabled() {
        return RadianteRenderer.isRayTracingEnabled();
    }

    /**
     * The sky's rotation for a sun angle (radians, Minecraft's {@code SUN_ANGLE} attribute): the sun sits on its
     * local +y, the moon on -y, and x is the axis the sky turns around. It follows the player's sun path settings
     * (lean and turn), so it is the sun the renderer draws and lights the world with.
     */
    public static Matrix4f celestialTransform(float sunAngleRadians) {
        return pathTransform(Options.sunPathTilt, Options.sunPathAzimuth, sunAngleRadians);
    }

    /**
     * The moon's counterpart of {@link #celestialTransform}: the moon sits on its local -y and x is the axis its
     * path turns around. It is the sun's own unless the player gave the moon a path of its own; either way the moon
     * keeps the hour opposite the sun's, so it rises as the sun sets.
     */
    public static Matrix4f moonTransform(float sunAngleRadians) {
        return Options.moonFollowsSun ? celestialTransform(sunAngleRadians)
            : pathTransform(Options.moonPathTilt, Options.moonPathAzimuth, sunAngleRadians);
    }

    /**
     * A path leaning tilt degrees (positive: its highest point to the south, +z) and turned azimuth degrees around
     * the vertical (positive: clockwise seen from above, sunrise moving from the east towards the south). The lean
     * turns about the east-west axis, so the body still rises and sets on the horizon at the same hours.
     */
    private static Matrix4f pathTransform(int tilt, int azimuth, float sunAngleRadians) {
        Matrix4f transform = new Matrix4f();
        if (azimuth != 0) {
            transform.rotateY((float) Math.toRadians(-azimuth));
        }
        return transform
            .rotateX((float) Math.toRadians(tilt))
            .rotateY((float) Math.toRadians(-90.0))
            .rotateX(sunAngleRadians);
    }

    /** Unit vector from the world towards the sun, in world space. */
    public static Vec3 sunDirection(float sunAngleRadians) {
        Vector3f direction = celestialTransform(sunAngleRadians).transformDirection(new Vector3f(0.0f, 1.0f, 0.0f))
            .normalize();
        return new Vec3(direction.x(), direction.y(), direction.z());
    }

    /** Unit vector from the world towards the moon, in world space. */
    public static Vec3 moonDirection(float sunAngleRadians) {
        Vector3f direction = moonTransform(sunAngleRadians).transformDirection(new Vector3f(0.0f, -1.0f, 0.0f))
            .normalize();
        return new Vec3(direction.x(), direction.y(), direction.z());
    }
}
