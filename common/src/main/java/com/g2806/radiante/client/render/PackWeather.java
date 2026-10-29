package com.g2806.radiante.client.render;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import org.joml.Vector4f;

/**
 * The air's wetness after rain, for shader packs (radiante-pt's {@code common/pack_world.glsl}). Radiante PT reads it
 * too: the air stays a little hazier after rain (aerial perspective, {@code common/biome_haze.glsl}).
 *
 * <p>x is Iris's {@code wetness}: the rain strength smoothed with Iris's default half-lives, {@code wetnessHalflife}
 * 600 and {@code drynessHalflife} 200, which Iris's SmoothedFloat reads as deciseconds, so it rises with a half-life of
 * a minute and falls with one of twenty seconds. y is how many seconds ago the rain eased off (fell below
 * {@link #STOPPED_BELOW}) after it had wetted the air: 0 while it rains, and {@link #NEVER} before any rain since the
 * world was joined. Both stand still while the game is paused.
 */
public final class PackWeather {

    private static final float RISE_HALF_LIFE = 60.0f;
    private static final float FALL_HALF_LIFE = 20.0f;
    /** Rain under this strength counts as stopped. */
    private static final float STOPPED_BELOW = 0.2f;
    /** Wetness the rain must have reached for its end to count as rain that stopped. */
    private static final float WET_ENOUGH = 0.25f;
    /** Seconds since rain when there has been none. */
    public static final float NEVER = 1.0e6f;

    private static float wetness;
    private static float stoppedFor = NEVER;
    private static boolean wasWet;
    private static Object level;
    private static long lastNanos;

    private PackWeather() {
    }

    /** Call once per frame. Returns x wetness, y seconds since the rain stopped, zw unused. */
    public static Vector4f update(Minecraft minecraft, float partialTicks) {
        long now = System.nanoTime();
        float seconds = lastNanos == 0L ? 0.0f : Math.min((now - lastNanos) / 1.0e9f, 1.0f);
        lastNanos = now;
        ClientLevel world = minecraft.level;
        if (world != level) {
            // A new world starts dry, as Iris's wetness does when its pipeline is created.
            level = world;
            wetness = 0.0f;
            stoppedFor = NEVER;
            wasWet = false;
        }
        if (world == null) {
            return new Vector4f(0.0f, NEVER, 0.0f, 0.0f);
        }
        if (minecraft.isPaused()) {
            seconds = 0.0f;
        }
        float rain = world.getRainLevel(partialTicks);
        float halfLife = rain > wetness ? RISE_HALF_LIFE : FALL_HALF_LIFE;
        wetness += (rain - wetness) * (1.0f - (float) Math.pow(0.5, seconds / halfLife));
        if (rain >= STOPPED_BELOW) {
            // A new shower: whether it wets the air is its own business, not the last one's.
            if (stoppedFor != 0.0f) {
                wasWet = false;
            }
            stoppedFor = 0.0f;
            wasWet |= wetness >= WET_ENOUGH;
        } else {
            // A shower too short to wet the air counts as no rain at all.
            stoppedFor = wasWet ? Math.min(stoppedFor + seconds, NEVER) : NEVER;
        }
        return new Vector4f(wetness, stoppedFor, 0.0f, 0.0f);
    }
}
