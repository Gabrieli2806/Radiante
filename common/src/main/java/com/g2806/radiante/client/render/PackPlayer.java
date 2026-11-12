package com.g2806.radiante.client.render;

import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import org.joml.Vector4f;

/**
 * How the player is doing, for shader packs (radiante-pt's {@code common/pack_world.glsl}): what a pack needs for a
 * red flash when hurt, a picture that drains of colour with little health, or a blur at a sprint. Radiante PT itself
 * reads none of it.
 *
 * <p>{@link #state()}: x health, y the flash of a hit, z food, w air, each 0 to 1. {@link #motion()}: x the speed in
 * blocks a second, y how much the player is sprinting, z how much they are on the ground, w how much they are hurt
 * overall (1 with no health left), all eased so an effect tied to them fades instead of switching. Everything stands
 * still while the game is paused, and is a healthy player standing still without a world.
 */
public final class PackPlayer {

    /** Seconds over which the eased values come half way to where they are going. */
    private static final float HALF_LIFE = 0.15f;
    /** The flash of a hit dies away over this long. */
    private static final float HIT_SECONDS = 0.6f;

    private static final Vector4f STATE = new Vector4f(1.0f, 0.0f, 1.0f, 1.0f);
    private static final Vector4f MOTION = new Vector4f(0.0f, 0.0f, 1.0f, 0.0f);

    private static float health = 1.0f;
    private static float lastHealth = 1.0f;
    private static float hit;
    private static float speed;
    private static float sprint;
    private static float ground = 1.0f;
    private static Object player;
    private static long lastNanos;

    private PackPlayer() {
    }

    /** Call once per frame, before {@link #state()} and {@link #motion()}. */
    public static void update(Minecraft minecraft) {
        long now = System.nanoTime();
        float seconds = lastNanos == 0L ? 0.0f : Math.min((now - lastNanos) / 1.0e9f, 0.25f);
        lastNanos = now;
        LocalPlayer current = minecraft.player;
        if (current == null) {
            player = null;
            STATE.set(1.0f, 0.0f, 1.0f, 1.0f);
            MOTION.set(0.0f, 0.0f, 1.0f, 0.0f);
            return;
        }
        float nowHealth = Math.clamp(current.getHealth() / Math.max(current.getMaxHealth(), 1.0f), 0.0f, 1.0f);
        if (current != player) {
            // A new player (a world joined, a respawn) starts as it is: no flash for the health it comes in with.
            player = current;
            health = nowHealth;
            lastHealth = nowHealth;
            hit = 0.0f;
            speed = 0.0f;
            sprint = 0.0f;
            ground = 1.0f;
        }
        if (minecraft.isPaused()) {
            seconds = 0.0f;
        }
        // A drop in health is a hit, the stronger the more it took; it then dies away.
        if (nowHealth < lastHealth - 1.0e-4f) {
            hit = Math.max(hit, Math.clamp(0.35f + (lastHealth - nowHealth) * 4.0f, 0.0f, 1.0f));
        } else {
            hit = Math.max(0.0f, hit - seconds / HIT_SECONDS);
        }
        lastHealth = nowHealth;
        float blend = 1.0f - (float) Math.pow(0.5, seconds / HALF_LIFE);
        health += (nowHealth - health) * blend;
        // Blocks a second, from the distance covered in the last tick.
        double dx = current.getX() - current.xo;
        double dy = current.getY() - current.yo;
        double dz = current.getZ() - current.zo;
        float nowSpeed = (float) Math.sqrt(dx * dx + dy * dy + dz * dz) * 20.0f;
        speed += (Math.min(nowSpeed, 100.0f) - speed) * blend;
        sprint += ((current.isSprinting() ? 1.0f : 0.0f) - sprint) * blend;
        ground += ((current.onGround() ? 1.0f : 0.0f) - ground) * blend;
        float food = Math.clamp(current.getFoodData().getFoodLevel() / 20.0f, 0.0f, 1.0f);
        float air = Math.clamp(current.getAirSupply() / (float) Math.max(current.getMaxAirSupply(), 1), 0.0f, 1.0f);
        STATE.set(health, hit, food, air);
        MOTION.set(speed, sprint, ground, 1.0f - health);
    }

    /** x health, y the flash of a hit, z food, w air, each 0 to 1. */
    public static Vector4f state() {
        return new Vector4f(STATE);
    }

    /** x speed in blocks a second, y sprinting, z on the ground, w how hurt (1 - health), eased. */
    public static Vector4f motion() {
        return new Vector4f(MOTION);
    }
}
