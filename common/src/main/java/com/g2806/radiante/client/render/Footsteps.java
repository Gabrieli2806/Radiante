package com.g2806.radiante.client.render;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.EntityTypes;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.phys.Vec3;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Where the feet of the players and mobs come down this frame, for everything that answers a step: the prints in
 * snow (SnowPrints) and the rings on puddles (WaterRipples). One place decides what a step is, so the snow and the
 * water answer the same steps alike.
 *
 * <p>A foot comes down each time the walk animation swings the legs to the end of their stride, as the models swing
 * them (HumanoidModel, QuadrupedModel: by the cosine of 0.6662 times the animation's position): one foot of a
 * two-legged walker, the diagonal pair of a four-legged one. Landing from a jump or a fall brings every foot down at
 * once, side by side; a slime lands on its whole body. Things that fly, swim or have no legs leave no steps.
 *
 * <p>Feet out of the water stay wet for {@link #WET_STEPS} steps; each footfall says how many are left.
 */
public final class Footsteps {

    /** One foot, or a slime's body, coming down. */
    public static final class Footfall {
        public int entityId;
        public double x;
        public double y;
        public double z;
        /** The way the walker faces, radians (Minecraft's yaw). */
        public float yaw;
        /** How wide the foot is, in pixels of a sixteenth of a block. */
        public int size;
        /** The walker's width, for how hard a step strikes a puddle. */
        public float width;
        /** Come down from a jump or a fall rather than in the walk. */
        public boolean landing;
        /** Steps the water on the feet still lasts for, counting this one; 0 when they are dry. */
        public int wetLeft;
        /** The first of the feet that come down together: a step, or a landing, counts once. */
        public boolean first;
    }

    /** Steps feet out of the water stay wet for. */
    public static final int WET_STEPS = 8;

    private enum Legs { TWO, FOUR, BODY, NONE }

    private static final double HALF_STRIDE = Math.PI / 0.6662;
    /** A player's leg: how far it is from the middle of the body, and how long it is, in blocks. */
    private static final double LEG_SIDE = 1.9 / 16.0;
    private static final double LEG_LENGTH = 12.0 / 16.0;
    /** Seconds in the air before coming down counts as a landing, not a step down a slab. */
    private static final double AIRBORNE_LANDING = 0.15;
    private static final double RANGE_SQ = 48.0 * 48.0;

    /** No feet to leave a step with: flying, swimming and legless things. */
    private static final Set<EntityType<?>> FOOTLESS = Set.of(EntityTypes.BAT, EntityTypes.BEE, EntityTypes.BLAZE,
        EntityTypes.GHAST, EntityTypes.HAPPY_GHAST, EntityTypes.VEX, EntityTypes.ALLAY, EntityTypes.PHANTOM,
        EntityTypes.SHULKER, EntityTypes.GUARDIAN, EntityTypes.ELDER_GUARDIAN, EntityTypes.SQUID, EntityTypes.GLOW_SQUID,
        EntityTypes.COD, EntityTypes.SALMON, EntityTypes.PUFFERFISH, EntityTypes.TROPICAL_FISH, EntityTypes.TADPOLE,
        EntityTypes.DOLPHIN, EntityTypes.ARMOR_STAND, EntityTypes.BREEZE, EntityTypes.SNOW_GOLEM,
        EntityTypes.ENDER_DRAGON, EntityTypes.WITHER, EntityTypes.SILVERFISH, EntityTypes.ENDERMITE);
    /** Four-legged things that are not animals, and animals that walk on two legs. */
    private static final Set<EntityType<?>> FOUR_LEGGED = Set.of(EntityTypes.CREEPER, EntityTypes.SPIDER,
        EntityTypes.CAVE_SPIDER, EntityTypes.RAVAGER, EntityTypes.ZOGLIN);
    private static final Set<EntityType<?>> TWO_LEGGED_ANIMALS = Set.of(EntityTypes.CHICKEN, EntityTypes.PARROT,
        EntityTypes.STRIDER);

    private static final class Walker {
        long step;
        long seen;
        double airborneSince = Double.NaN;
        int wetSteps;
    }

    private static final Map<Integer, Walker> WALKERS = new HashMap<>();
    private static final List<Footfall> FALLS = new ArrayList<>();
    private static final List<Footfall> POOL = new ArrayList<>();
    private static ClientLevel lastLevel;
    private static long frame;

    private Footsteps() {
    }

    /** The footfalls of this frame, made by {@link #update}. */
    public static List<Footfall> footfalls() {
        return FALLS;
    }

    /** Finds the footfalls of this frame; once a frame, before the effects that read them. */
    public static void update(Minecraft minecraft, Vec3 cameraPos, long gameTime, float partialTick) {
        POOL.addAll(FALLS);
        FALLS.clear();
        ClientLevel level = minecraft.level;
        if (level != lastLevel) {
            lastLevel = level;
            WALKERS.clear();
        }
        if (level == null || !RadianteRenderer.isRayTracingEnabled()) {
            return;
        }
        double now = (gameTime + partialTick) / 20.0;
        frame++;
        for (Entity entity : level.entitiesForRendering()) {
            if (!(entity instanceof LivingEntity living) || entity.isSpectator()) {
                continue;
            }
            double x = Mth.lerp(partialTick, entity.xo, entity.getX());
            double y = Mth.lerp(partialTick, entity.yo, entity.getY());
            double z = Mth.lerp(partialTick, entity.zo, entity.getZ());
            if (cameraPos.distanceToSqr(x, y, z) > RANGE_SQ) {
                continue;
            }
            Legs legs = legsOf(living);
            if (legs == Legs.NONE) {
                continue;
            }
            long step = Math.round(living.walkAnimation.position(partialTick) / HALF_STRIDE);
            Walker walker = WALKERS.get(entity.getId());
            if (walker == null) {
                walker = new Walker();
                walker.step = step;
                WALKERS.put(entity.getId(), walker);
            }
            walker.seen = frame;
            if (entity.isInWater()) {
                walker.wetSteps = WET_STEPS;
                walker.step = step;
                walker.airborneSince = Double.NaN;
                continue;
            }
            if (!entity.onGround()) {
                if (Double.isNaN(walker.airborneSince)) {
                    walker.airborneSince = now;
                }
                walker.step = step;
                continue;
            }
            boolean landing = !Double.isNaN(walker.airborneSince) && now - walker.airborneSince >= AIRBORNE_LANDING;
            walker.airborneSince = Double.NaN;
            float yaw = (float) Math.toRadians(Mth.rotLerp(partialTick, living.yBodyRotO, living.yBodyRot));
            if (landing) {
                walker.step = step;
                land(living, legs, walker, x, y, z, yaw);
                continue;
            }
            if (step == walker.step) {
                continue;
            }
            walker.step = step;
            float swing = living.walkAnimation.speed(partialTick);
            if (swing < 0.05f || legs == Legs.BODY) {
                continue;
            }
            stride(living, legs, walker, x, y, z, yaw, swing, (step & 1L) == 0L);
        }
        if ((frame & 255) == 0) {
            WALKERS.values().removeIf(walker -> frame - walker.seen > 600);
        }
    }

    /** A step in the walk: the foot or the pair of feet the legs bring down, ahead by as far as they swing. */
    private static void stride(LivingEntity entity, Legs legs, Walker walker, double x, double y, double z, float yaw,
                               float swing, boolean even) {
        float width = entity.getBbWidth();
        double reach = Math.sin(Math.min(1.4 * swing, 1.2));
        int wetLeft = spendWet(walker);
        if (legs == Legs.TWO) {
            double ahead = reach * LEG_LENGTH * entity.getBbHeight() / 1.8;
            foot(entity, x, y, z, yaw, ahead, (even ? 1.0 : -1.0) * LEG_SIDE * width / 0.6, legSize(legs, width),
                false, wetLeft);
        } else {
            // A front foot with the hind one across from it, as QuadrupedModel swings them.
            double ahead = reach * 0.4 * entity.getBbHeight() * 0.5;
            double along = 0.45 * width;
            double side = (even ? 1.0 : -1.0) * 0.25 * width;
            int size = legSize(legs, width);
            foot(entity, x, y, z, yaw, along + ahead, side, size, false, wetLeft);
            foot(entity, x, y, z, yaw, -along + ahead, -side, size, false, wetLeft);
        }
    }

    /** Down from a jump or a fall: every foot at once, under the body, or the whole of a slime. */
    private static void land(LivingEntity entity, Legs legs, Walker walker, double x, double y, double z, float yaw) {
        float width = entity.getBbWidth();
        int wetLeft = spendWet(walker);
        switch (legs) {
            case TWO -> {
                double side = LEG_SIDE * width / 0.6;
                foot(entity, x, y, z, yaw, 0.0, side, legSize(legs, width), true, wetLeft);
                foot(entity, x, y, z, yaw, 0.0, -side, legSize(legs, width), true, wetLeft);
            }
            case FOUR -> {
                double along = 0.45 * width;
                double side = 0.25 * width;
                int size = legSize(legs, width);
                foot(entity, x, y, z, yaw, along, side, size, true, wetLeft);
                foot(entity, x, y, z, yaw, along, -side, size, true, wetLeft);
                foot(entity, x, y, z, yaw, -along, side, size, true, wetLeft);
                foot(entity, x, y, z, yaw, -along, -side, size, true, wetLeft);
            }
            default -> foot(entity, x, y, z, yaw, 0.0, 0.0,
                Math.min(Math.max(2 * Math.round(width * 6.4f), 4), 24), true, wetLeft);
        }
    }

    private static int spendWet(Walker walker) {
        int left = walker.wetSteps;
        walker.wetSteps = Math.max(0, walker.wetSteps - 1);
        return left;
    }

    /** A leg's width in pixels: a player's or a cow's four, a golem's six, a chicken's or a wolf's two. */
    private static int legSize(Legs legs, float width) {
        if (legs == Legs.TWO) {
            return width >= 1.0f ? 6 : width >= 0.5f ? 4 : 2;
        }
        return width >= 1.6f ? 6 : width >= 0.8f ? 4 : 2;
    }

    /** One footfall, ahead of the body and to its side (left positive) along the way it faces. */
    private static void foot(Entity entity, double x, double y, double z, float yaw, double ahead, double side,
                             int size, boolean landing, int wetLeft) {
        Footfall fall = POOL.isEmpty() ? new Footfall() : POOL.remove(POOL.size() - 1);
        // Minecraft's frame: the way faced is (-sin, cos) of the yaw, and (cos, sin) is the left.
        fall.entityId = entity.getId();
        fall.x = x - Math.sin(yaw) * ahead + Math.cos(yaw) * side;
        fall.y = y;
        fall.z = z + Math.cos(yaw) * ahead + Math.sin(yaw) * side;
        fall.yaw = yaw;
        fall.size = size;
        fall.width = entity.getBbWidth();
        fall.landing = landing;
        fall.wetLeft = wetLeft;
        // An entity makes one step or one landing a frame, its feet one after another in the list.
        fall.first = FALLS.isEmpty() || FALLS.get(FALLS.size() - 1).entityId != entity.getId();
        FALLS.add(fall);
    }

    private static Legs legsOf(LivingEntity entity) {
        EntityType<?> type = entity.getType();
        if (type == EntityTypes.SLIME || type == EntityTypes.MAGMA_CUBE) {
            return Legs.BODY;
        }
        if (FOOTLESS.contains(type) || entity.getBbWidth() < 0.3f) {
            return Legs.NONE;
        }
        if (FOUR_LEGGED.contains(type)) {
            return Legs.FOUR;
        }
        if (entity instanceof net.minecraft.world.entity.animal.Animal && !TWO_LEGGED_ANIMALS.contains(type)) {
            return Legs.FOUR;
        }
        return Legs.TWO;
    }
}
