package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.blockentity.state.BlockEntityRenderState;
import net.minecraft.client.renderer.entity.state.EntityRenderState;
import net.minecraft.core.BlockPos;
import net.minecraft.world.phys.Vec3;
import net.minecraft.client.renderer.rendertype.RenderType;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.client.renderer.state.level.LevelRenderState;
import net.minecraft.util.Mth;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.chunk.LevelChunk;

import static com.g2806.radiante.client.render.EntityUpload.*;
import static com.g2806.radiante.client.render.EntityManager.*;

/**
 * Block entities as traced geometry: collecting their states around the camera, the beacons far from it, the ones
 * that light their surroundings, and keeping what did not change from frame to frame (BlockEntityCache).
 *
 * <p>This was part of {@code EntityManager}.
 */
public final class EntityBlockEntities {

    private EntityBlockEntities() {
    }

    /**
     * Minecraft only extracts block entities from the chunk sections it decided to draw, and the ray tracer replaces
     * that pass entirely, so its list holds nothing but the handful that render from any distance, such as beacons.
     * Everything placed in the world - signs, chests, banners, end portals - has to be gathered here instead, from
     * the chunks around the camera.
     */
    static List<BlockEntityRenderState> collectBlockEntityStates(Minecraft minecraft,
        LevelRenderState levelRenderState, CameraRenderState cameraState) {
        List<BlockEntityRenderState> states = new ArrayList<>(levelRenderState.blockEntityRenderStates);
        if (minecraft.level == null) {
            return states;
        }

        Vec3 camera = cameraState.pos;
        int centerX = Mth.floor(camera.x()) >> 4;
        int centerZ = Mth.floor(camera.z()) >> 4;
        int radius = Math.min(BLOCK_ENTITY_CHUNK_RADIUS, minecraft.options.getEffectiveRenderDistance());
        float partialTicks = levelRenderState.worldPartialTicks;

        for (int x = centerX - radius; x <= centerX + radius; x++) {
            for (int z = centerZ - radius; z <= centerZ + radius; z++) {
                LevelChunk chunk = minecraft.level.getChunkSource().getChunk(x, z, false);
                if (chunk == null) {
                    continue;
                }
                for (Map.Entry<BlockPos, BlockEntity> entry : chunk.getBlockEntities().entrySet()) {
                    BlockPos pos = entry.getKey();
                    if (pos.distToCenterSqr(camera) > BLOCK_ENTITY_RANGE * BLOCK_ENTITY_RANGE) {
                        continue;
                    }
                    // One that has not changed is handed back as it was; see BlockEntityCache.
                    if (BlockEntityCache.reuse(pos, entry.getValue(), PENDING)) {
                        continue;
                    }
                    BlockEntityRenderState state = minecraft.getBlockEntityRenderDispatcher()
                        .tryExtractRenderState(entry.getValue(), partialTicks, null, false);
                    if (state != null) {
                        // A banner sways all the time, which made every one of them new geometry every frame.
                        // Away from the camera the sway is not worth that: it hangs still and is kept.
                        if (state instanceof net.minecraft.client.renderer.blockentity.state.BannerRenderState banner
                            && pos.distToCenterSqr(camera) > BANNER_SWAY_RANGE * BANNER_SWAY_RANGE) {
                            banner.phase = 0.0f;
                        }
                        states.add(state);
                        CACHE_CANDIDATES.put(pos, entry.getValue());
                    }
                }
            }
        }
        addFarBeacons(minecraft, camera, partialTicks, states);
        return states;
    }

    /** Beacons in loaded chunks, found by a sweep every so often; see addFarBeacons. */
    static final List<BlockPos> BEACONS = new ArrayList<>();
    static final int BEACON_SWEEP_FRAMES = 40;
    static int beaconSweepCountdown;
    static Object beaconSweepLevel;

    /**
     * A beacon's beam is seen from as far as its chunk is loaded, unlike every other block entity, which ends at
     * {@link #BLOCK_ENTITY_RANGE}: without this the beam went out on walking away from it. Looking through every
     * loaded chunk each frame would cost too much, so the beacons are found by a sweep now and then and only their
     * render states are taken each frame.
     */
    static void addFarBeacons(Minecraft minecraft, Vec3 camera, float partialTicks,
        List<BlockEntityRenderState> states) {
        if (beaconSweepLevel != minecraft.level) {
            beaconSweepLevel = minecraft.level;
            beaconSweepCountdown = 0;
        }
        if (beaconSweepCountdown-- <= 0) {
            beaconSweepCountdown = BEACON_SWEEP_FRAMES;
            BEACONS.clear();
            int centerX = Mth.floor(camera.x()) >> 4;
            int centerZ = Mth.floor(camera.z()) >> 4;
            int radius = minecraft.options.getEffectiveRenderDistance();
            for (int x = centerX - radius; x <= centerX + radius; x++) {
                for (int z = centerZ - radius; z <= centerZ + radius; z++) {
                    LevelChunk chunk = minecraft.level.getChunkSource().getChunk(x, z, false);
                    if (chunk == null) {
                        continue;
                    }
                    for (BlockEntity blockEntity : chunk.getBlockEntities().values()) {
                        if (blockEntity instanceof net.minecraft.world.level.block.entity.BeaconBlockEntity) {
                            BEACONS.add(blockEntity.getBlockPos());
                        }
                    }
                }
            }
        }
        if (BEACONS.isEmpty()) {
            return;
        }
        java.util.Set<BlockPos> present = new java.util.HashSet<>();
        for (BlockEntityRenderState state : states) {
            present.add(state.blockPos);
        }
        for (BlockPos pos : BEACONS) {
            if (present.contains(pos) || !(minecraft.level.getBlockEntity(pos)
                instanceof net.minecraft.world.level.block.entity.BeaconBlockEntity beacon)) {
                continue;
            }
            BlockEntityRenderState state = minecraft.getBlockEntityRenderDispatcher()
                .tryExtractRenderState(beacon, partialTicks, null, false);
            if (state != null) {
                states.add(state);
            }
        }
    }

    /**
     * How much of an entity's brightness comes from the entity rather than from the world. Vanilla mobs that light
     * themselves do it by overriding the block light handed to the renderer (GlowSquidRenderer returns 15, and so
     * do the blaze and the magma cube), which a lightmap would pick up and a path tracer cannot. Whatever that
     * light exceeds the block light actually at the entity's feet is taken as the entity's own glow.
     */
    static float selfLitEmission(Minecraft minecraft, EntityRenderState state) {
        if (minecraft.level == null) {
            return 0.0f;
        }

        // The wither and its skulls are drawn full bright in vanilla (their renderers report block light 15) but give
        // off no light.
        if (state instanceof net.minecraft.client.renderer.entity.state.WitherRenderState
            || state instanceof net.minecraft.client.renderer.entity.state.WitherSkullRenderState) {
            return 0.0f;
        }
        int ownBlockLight = net.minecraft.util.LightCoordsUtil.block(state.lightCoords);
        if (ownBlockLight <= 0) {
            return 0.0f;
        }

        // Vanilla samples the light for an entity at a block this does not always agree on - a mob by a torch can
        // report a level or two more than the block under it - so the brightest of its feet, middle and head is
        // what counts, and only a wide gap on top of that is the entity lighting itself.
        int worldBlockLight = 0;
        for (double offset : SELF_LIT_SAMPLE_HEIGHTS) {
            BlockPos pos = BlockPos.containing(state.x, state.y + state.boundingBoxHeight * offset, state.z);
            worldBlockLight = Math.max(worldBlockLight,
                minecraft.level.getBrightness(net.minecraft.world.level.LightLayer.BLOCK, pos));
        }

        int excess = ownBlockLight - worldBlockLight;
        if (excess < SELF_LIT_MIN_EXCESS) {
            return 0.0f;
        }
        // A display entity given a brightness of its own (maps and servers build signs, statues and whole walls from
        // them) is drawn unshaded in vanilla but lights nothing. At a mob's glow a wall of them became a wall of
        // lamps: the room washed out and the exposure with it. A low glow keeps them readable in the dark without
        // turning them into light sources.
        if (state instanceof net.minecraft.client.renderer.entity.state.DisplayEntityRenderState) {
            return Glow.ofLevel(Math.min(excess, DISPLAY_MAX_GLOW_LEVEL));
        }
        return Glow.ofLevel(excess);
    }

    /**
     * Whether an entity vanilla culled (out of the view frustum, or in a section it would not draw) is kept anyway:
     * one that gives off light, within the Entity Light Reach of the camera. Its light reaches what is on screen
     * whichever way the camera points, as a torch's does, and turning away used to switch the room's light off.
     * Only light sources are kept, the rest still go with the frustum. Display entities light nothing worth the cost
     * (see {@link #selfLitEmission}).
     */
    public static boolean keepsLightOutOfView(net.minecraft.world.entity.Entity entity, double cameraX,
        double cameraY, double cameraZ, float partialTick) {
        Minecraft minecraft = Minecraft.getInstance();
        if (minecraft.level == null || entity.isRemoved() || entity.isInvisible()) {
            return false;
        }
        double reach = Options.entityLightReach;
        if (entity.distanceToSqr(cameraX, cameraY, cameraZ) > reach * reach
            || !entity.shouldRender(cameraX, cameraY, cameraZ)) {
            return false;
        }
        if (entity instanceof net.minecraft.world.entity.decoration.GlowItemFrame
            || entity instanceof net.minecraft.world.entity.boss.enderdragon.EndCrystal) {
            return true;
        }
        if (entity instanceof net.minecraft.world.entity.Display
            || entity instanceof net.minecraft.world.entity.boss.wither.WitherBoss
            || entity instanceof net.minecraft.world.entity.projectile.hurtingprojectile.WitherSkull) {
            return false;
        }
        // The same test as selfLitEmission, on the entity: its renderer reports more block light than is there.
        int ownBlockLight = net.minecraft.util.LightCoordsUtil.block(
            minecraft.getEntityRenderDispatcher().getPackedLightCoords(entity, partialTick));
        if (ownBlockLight < SELF_LIT_MIN_EXCESS) {
            return false;
        }
        Vec3 position = entity.getPosition(partialTick);
        int worldBlockLight = 0;
        for (double offset : SELF_LIT_SAMPLE_HEIGHTS) {
            BlockPos pos = BlockPos.containing(position.x, position.y + entity.getBbHeight() * offset, position.z);
            worldBlockLight = Math.max(worldBlockLight,
                minecraft.level.getBrightness(net.minecraft.world.level.LightLayer.BLOCK, pos));
        }
        return ownBlockLight - worldBlockLight >= SELF_LIT_MIN_EXCESS;
    }

    static void collectBlockEntity(Minecraft minecraft, CameraRenderState cameraState,
        BlockEntityRenderState state) {
        COLLECTOR.reset();
        POSE_STACK.setIdentity();

        try {
            minecraft.getBlockEntityRenderDispatcher().submit(state, POSE_STACK, COLLECTOR, cameraState);
        } catch (RuntimeException e) {
            // Swallowing this silently once hid the reason a whole kind of block entity never appeared.
            if (DEBUG_FAILED_BLOCK_ENTITIES.add(state.getClass().getSimpleName())) {
                RadianteRenderer.LOGGER.warn("Block entity {} could not be collected", state.getClass().getSimpleName(),
                    e);
            }
            return;
        }

        if (COLLECTOR.isEmpty()) {
            return;
        }

        BlockPos pos = state.blockPos;
        int before = PENDING.size();
        DevProfiler.kind(state);
        addPending(blockId(pos) ^ 0x5BD1E995, pos.getX(), pos.getY(), pos.getZ(), RAY_TRACING_WORLD, true);
        DevProfiler.kind(null);
        BlockEntity cached = CACHE_CANDIDATES.get(pos);
        if (cached != null) {
            BlockEntityCache.collected(pos, cached, PENDING.subList(before, PENDING.size()), ARENA.addressOf(0L));
        }
        if (Options.debugLogging && DEBUG_BLOCK_ENTITIES < 20) {
            for (RenderType type : COLLECTOR.layers().keySet()) {
                RenderTypeInfo info = RenderTypeInfo.of(type);
                if (!info.groupName().equals("Entity")) {
                    DEBUG_BLOCK_ENTITIES++;
                    RadianteRenderer.LOGGER.info("special block entity at {}: group={} geometry={} vertices={} added={}",
                        pos, info.groupName(), info.geometryType(), COLLECTOR.layers().get(type).vertexCount(),
                        PENDING.size() - before);
                }
            }
        }
    }
}
