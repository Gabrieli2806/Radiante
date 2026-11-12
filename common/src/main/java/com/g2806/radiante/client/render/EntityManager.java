package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.proxy.world.EntityProxy;
import com.mojang.blaze3d.vertex.PoseStack;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.client.particle.SingleQuadParticle;
import net.minecraft.client.renderer.blockentity.state.BlockEntityRenderState;
import net.minecraft.client.renderer.entity.state.EntityRenderState;
import net.minecraft.client.renderer.state.level.QuadParticleRenderState;
import net.minecraft.core.BlockPos;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.world.phys.Vec3;
import net.minecraft.client.renderer.rendertype.RenderType;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.client.renderer.state.level.LevelRenderState;
import net.minecraft.client.renderer.state.level.PlayerRenderState;
import net.minecraft.util.Mth;
import net.minecraft.world.level.GameType;
import static com.g2806.radiante.client.render.EntityUpload.*;
import static com.g2806.radiante.client.render.EntityHands.*;
import static com.g2806.radiante.client.render.EntityWeather.*;
import static com.g2806.radiante.client.render.EntityBlockEntities.*;

/**
 * Feeds mobs, the player and the first person hands into the renderer. Minecraft submits them once per frame
 * and the geometry is rebuilt every frame, which is what the renderer expects for dynamic entities.
 */
public final class EntityManager {

    static final EntityCollector COLLECTOR =
        com.g2806.radiante.platform.RadiantePlatform.INSTANCE.createEntityCollector();
    /**
     * Vivecraft's hands and what they hold (VivecraftHandsMixin). Vivecraft submits them during Minecraft's level
     * render, after the frame has been traced, so they are kept here and traced with the next frame - the other
     * eye's, in VR - at the world position they were drawn at.
     */
    static final EntityCollector VR_HANDS =
        com.g2806.radiante.platform.RadiantePlatform.INSTANCE.createEntityCollector();
    static final int VR_HANDS_ID = "radiante:vr_hands".hashCode();
    static boolean vrHandsCaptured;
    static double vrHandsX;
    static double vrHandsY;
    static double vrHandsZ;
    /**
     * What other mods submit at the end of Minecraft's feature submission (LevelRenderer.submitFeatures) while the
     * world is traced: Animated Doors draws its swinging doors there, as moving blocks, with the door itself taken
     * out of the terrain. That submission normally runs in the level render over the traced picture, after the
     * frame was traced; LevelRendererSkipMixin runs it once more, with nothing of Minecraft's own in it, before the
     * frame is traced, and what the mods add then is kept here and traced in the same frame. Traced a frame later,
     * the door vanished for a frame as its swing began.
     */
    static final EntityCollector[] MOD_SUBMITS = {
        com.g2806.radiante.platform.RadiantePlatform.INSTANCE.createEntityCollector(),
        com.g2806.radiante.platform.RadiantePlatform.INSTANCE.createEntityCollector()};
    static final int MOD_SUBMITS_ID = "radiante:mod_submits".hashCode();
    /** The collector being filled; the other one holds the last capture that had anything in it. */
    static int modSubmitsCurrent;
    static boolean modSubmitsCaptured;
    static final double[] MOD_SUBMITS_CAMERA = new double[6];
    /**
     * Frames the last capture is kept on after mods stop submitting. At the end of a door's swing the terrain gets
     * the door back only once its section is rebuilt and its geometry rebuilt for the tracer, which can take well
     * over the 8 frames first tried here, and in between the door vanished.
     */
    static final int MOD_SUBMITS_HOLD_FRAMES = 20;
    static int modSubmitsHeld;

    /**
     * Starts the capture, before the frame is traced: what mods add to the feature submission goes into the returned
     * collector and is traced in this same frame (LevelRendererSkipMixin's early pass).
     */
    public static EntityCollector beginModSubmits(Vec3 camera) {
        EntityCollector collector = MOD_SUBMITS[modSubmitsCurrent];
        collector.reset();
        collector.forward(null);
        modSubmitsCaptured = true;
        MOD_SUBMITS_CAMERA[modSubmitsCurrent * 3] = camera.x;
        MOD_SUBMITS_CAMERA[modSubmitsCurrent * 3 + 1] = camera.y;
        MOD_SUBMITS_CAMERA[modSubmitsCurrent * 3 + 2] = camera.z;
        return collector;
    }

    /** Ends the early pass: debug gizmos it gathered are Minecraft's own, drawn by the level render after it. */
    public static void endModSubmits() {
        MOD_SUBMITS[modSubmitsCurrent].drainGizmoGroups();
    }

    /**
     * The same mods submitting again in the level render over the traced picture: their geometry was traced already
     * and is dropped; lines and outlines still reach Minecraft's collector.
     */
    static final EntityCollector MOD_SUBMITS_DROPPED =
        com.g2806.radiante.platform.RadiantePlatform.INSTANCE.createEntityCollector();

    public static SubmitNodeCollector dropModSubmits(SubmitNodeCollector vanilla) {
        MOD_SUBMITS_DROPPED.reset();
        MOD_SUBMITS_DROPPED.drainGizmoGroups();
        MOD_SUBMITS_DROPPED.forward(vanilla);
        return MOD_SUBMITS_DROPPED;
    }

    static void collectModSubmits() {
        int current = modSubmitsCurrent;
        EntityCollector collector = MOD_SUBMITS[current];
        boolean fresh = modSubmitsCaptured;
        // Once: a frame without an early pass must not keep showing what was captured before.
        modSubmitsCaptured = false;
        if (fresh && !collector.isEmpty()) {
            addPending(collector, MOD_SUBMITS_ID, MOD_SUBMITS_CAMERA[current * 3], MOD_SUBMITS_CAMERA[current * 3 + 1],
                MOD_SUBMITS_CAMERA[current * 3 + 2], RAY_TRACING_WORLD, false);
            // Kept as it is for the frames after; the next capture goes into the other collector.
            modSubmitsCurrent = 1 - current;
            modSubmitsHeld = MOD_SUBMITS_HOLD_FRAMES;
            return;
        }
        if (modSubmitsHeld > 0) {
            modSubmitsHeld--;
            int held = 1 - current;
            addPending(MOD_SUBMITS[held], MOD_SUBMITS_ID, MOD_SUBMITS_CAMERA[held * 3], MOD_SUBMITS_CAMERA[held * 3 + 1],
                MOD_SUBMITS_CAMERA[held * 3 + 2], RAY_TRACING_WORLD, false);
        }
    }

    static final PoseStack POSE_STACK = new PoseStack();
    static final List<PendingEntity> PENDING = new ArrayList<>();
    static boolean queued;
    static int DEBUG_BLOCK_ENTITIES;
    static final java.util.Set<String> DEBUG_FAILED_BLOCK_ENTITIES = new java.util.HashSet<>();
    /** How far out block entities are gathered, in chunks and in blocks; beyond this they are too small to matter. */
    static final int BLOCK_ENTITY_CHUNK_RADIUS = 6;
    static final double BLOCK_ENTITY_RANGE = 80.0;
    /**
     * How far the writing on signs is traced. Past this a letter is smaller than a pixel of the traced picture,
     * and every sign's text is an instance of its own whose letters are cut out by an any-hit shader: around a
     * spawn with hundreds of signs that was a large share of the frame for nothing anyone could read.
     */
    static final double BLOCK_TEXT_RANGE = 40.0;
    /** How far from the camera banners still sway. */
    static final double BANNER_SWAY_RANGE = 24.0;
    static final int END_CRYSTAL_TINT = 0xD9A6FF;
    /** Below this the difference is the lightmap disagreeing with the block below the entity, not a glow. */
    static final int SELF_LIT_MIN_EXCESS = 5;
    static final int DISPLAY_MAX_GLOW_LEVEL = 3;
    static final double[] SELF_LIT_SAMPLE_HEIGHTS = {0.0, 0.5, 1.0};
    static int DEBUG_TEXT_LAYERS;

    /** Masks the ray tracing shaders select geometry with. */
    static final int RAY_TRACING_WORLD = 0b00000001;
    /** Geometry camera rays skip in first person, while shadow rays and later bounces still see it. */
    static final int RAY_TRACING_PLAYER = 0b00000010;
    static final int RAY_TRACING_HAND = 0b00001000;
    /** Seen only by the glowing effect's outline rays (GLOW_OUTLINE_MASK in the shaders). */
    static final int RAY_TRACING_GLOW_OUTLINE = 0b00000100;
    static final int GLOW_OUTLINE_ID_SALT = 0x676C6F77;
    static final int RAY_TRACING_PARTICLE = 0b00100000;
    static final int RAY_TRACING_CLOUD = 0b01000000;
    static final int CLOUD_ID = "radiante:clouds".hashCode();
    static final CloudGeometry CLOUDS = new CloudGeometry();
    static final int NAME_TAG_ID_SALT = 0x6E616D65;
    static final int PARTICLES_ID = "radiante:particles".hashCode();
    static final int SHADOW_PARTICLES_ID = "radiante:shadow_particles".hashCode();
    /** The glow of a particle Minecraft draws at full light; see collectParticles. */
    static final float PARTICLE_GLOW = Glow.ofLevel(12);
    static final PBRVertexWriter PARTICLE_WRITER = new PBRVertexWriter(4096);
    static final int HAND_ID = "radiante:hand".hashCode();
    static final int PLAYER_SHADOW_ID = "radiante:player_shadow".hashCode();
    static final int WEATHER_ID = "radiante:weather".hashCode();
    static final int BREAKING_ID_SALT = 0x62726B21;
    static final int RAY_TRACING_WEATHER = 0b00010000;
    static final PBRVertexWriter WEATHER_WRITER = new PBRVertexWriter(4096);
    static final net.minecraft.resources.Identifier RAIN_TEXTURE =
        net.minecraft.resources.Identifier.withDefaultNamespace("textures/environment/rain.png");
    static final net.minecraft.resources.Identifier SNOW_TEXTURE =
        net.minecraft.resources.Identifier.withDefaultNamespace("textures/environment/snow.png");
    /** Vanilla's per-column facing: each sheet is turned side-on to the camera column it stands in. */
    static final float[] WEATHER_COLUMN_X = new float[32 * 32];
    static final float[] WEATHER_COLUMN_Z = new float[32 * 32];

    static {
        for (int z = 0; z < 32; z++) {
            for (int x = 0; x < 32; x++) {
                float deltaX = x - 16;
                float deltaZ = z - 16;
                float distance = Mth.length(deltaX, deltaZ);
                WEATHER_COLUMN_X[z * 32 + x] = distance == 0.0f ? 0.0f : -deltaZ / distance;
                WEATHER_COLUMN_Z[z * 32 + x] = distance == 0.0f ? 0.0f : deltaX / distance;
            }
        }
    }

    private EntityManager() {
    }

    /**
     * {@code vertices} is an offset into the frame arena, unless {@code directAddress} is set: then the vertices
     * are read from there as they are (a mesh kept between frames, like the clouds). {@code contentName} names the
     * content for keyed caching, or is null.
     */
    record PendingLayer(int geometryType, int textureId, int vertexCount, long vertices, String name,
        long directAddress, String contentName) {

        PendingLayer(int geometryType, int textureId, int vertexCount, long vertices, String name) {
            this(geometryType, textureId, vertexCount, vertices, name, 0L, null);
        }
    }

    /**
     * {@code cacheable}: geometry that usually stays the same from frame to frame (block entities). The native
     * side keeps the acceleration structure of such an entity while its content is unchanged instead of
     * rebuilding it every frame.
     */
    record PendingEntity(int id, double x, double y, double z, int rayTracingFlag,
        List<PendingLayer> layers, int prebuiltBlas) {

        PendingEntity(int id, double x, double y, double z, int rayTracingFlag, List<PendingLayer> layers) {
            this(id, x, y, z, rayTracingFlag, layers, PREBUILT_BLAS_NONE);
        }

        PendingEntity(int id, double x, double y, double z, int rayTracingFlag, List<PendingLayer> layers,
            boolean cacheable) {
            this(id, x, y, z, rayTracingFlag, layers, cacheable ? PREBUILT_BLAS_CACHEABLE : PREBUILT_BLAS_NONE);
        }
    }

    /** Tells native an entity may keep its acceleration structure while its geometry is unchanged. */
    static final int PREBUILT_BLAS_NONE = -1;
    static final int PREBUILT_BLAS_CACHEABLE = -2;
    /** Cached by the content name the layers carry, and moved by position alone; see Entities::KEYED_BLAS. */
    static final int PREBUILT_BLAS_KEYED = -3;

    public static void render(Minecraft minecraft, LevelRenderState levelRenderState) {
        if (minecraft.level == null) {
            return;
        }

        CameraRenderState cameraState = levelRenderState.cameraRenderState;
        PENDING.clear();
        // Last frame's vertex copies were consumed by the upload below; the arena starts over.
        ARENA.reset();
        BlockEntityCache.beginFrame(minecraft.level);
        CACHE_CANDIDATES.clear();

        DevProfiler.partsBegin();
        for (EntityRenderState state : levelRenderState.entityRenderStates) {
            collect(minecraft, cameraState, state);
        }
        DevProfiler.part(0);

        List<BlockEntityRenderState> blockEntityStates = collectBlockEntityStates(minecraft, levelRenderState,
            cameraState);
        DevProfiler.part(1);
        for (BlockEntityRenderState state : blockEntityStates) {
            collectBlockEntity(minecraft, cameraState, state);
        }
        DevProfiler.part(2);

        collectPlayerShadow(minecraft, levelRenderState, cameraState);
        collectBlockBreaking(minecraft, levelRenderState);
        collectParticles(levelRenderState, cameraState);
        collectExternal(levelRenderState, cameraState);
        collectModSubmits();
        collectWeather(levelRenderState, cameraState);
        OverlayLines.collectDebugGizmos(minecraft, cameraState);
        OverlayLines.collectBlockOutline(levelRenderState, cameraState);
        collectClouds(minecraft, levelRenderState, cameraState);
        collectHands(minecraft, levelRenderState, cameraState);
        collectVrHands();
        Vec3 eye = cameraState.pos;
        PENDING.removeIf(entity -> isBlockText(entity) && (entity.x() - eye.x()) * (entity.x() - eye.x())
            + (entity.y() - eye.y()) * (entity.y() - eye.y()) + (entity.z() - eye.z()) * (entity.z() - eye.z())
            > BLOCK_TEXT_RANGE * BLOCK_TEXT_RANGE);
        DevProfiler.part(3);

        upload(NativeGeometry.COORDINATE_WORLD);
        DevProfiler.part(4);

        if (queued) {
            queued = false;
            EntityProxy.build();
        }
        DevProfiler.part(5);
    }

    static void collect(Minecraft minecraft, CameraRenderState cameraState, EntityRenderState state) {
        // Entities that hang or stand where they were put: kept while their geometry stays the same, like block
        // entities (BlockEntityCache), instead of being written and built again every frame.
        boolean still = !state.appearsGlowing()
            && (state instanceof net.minecraft.client.renderer.entity.state.ItemFrameRenderState
                || state instanceof net.minecraft.client.renderer.entity.state.PaintingRenderState
                || state instanceof net.minecraft.client.renderer.entity.state.ArmorStandRenderState);
        if (still && BlockEntityCache.reuseEntity(historyId(state), state.x, state.y, state.z, PENDING)) {
            return;
        }

        COLLECTOR.reset();
        if (minecraft.player != null && ((EntityIdHolder) state).radiante$entityId() == minecraft.player.getId()) {
            configureHeldLights(minecraft, COLLECTOR, state);
        }
        net.minecraft.world.level.block.Block droppedLight = null;
        if (minecraft.level != null && com.g2806.radiante.client.pipeline.PipelineSettings.castsBedrockGlow()
            && minecraft.level.getEntity(((EntityIdHolder) state).radiante$entityId())
                instanceof net.minecraft.world.entity.item.ItemEntity item
            && item.getItem().getItem() instanceof net.minecraft.world.item.BlockItem blockItem
            && BedrockPointLights.colourOf(blockItem.getBlock()) != null) {
            droppedLight = blockItem.getBlock();
            COLLECTOR.groundPointLight(true);
        }
        POSE_STACK.setIdentity();

        // A glow item frame is lit from within in vanilla rather than by the world around it, and nothing in its
        // model says so; the entity does.
        float emission = 0.0f;
        if (state instanceof net.minecraft.client.renderer.entity.state.ItemFrameRenderState frame
            && frame.isGlowFrame) {
            emission = Glow.GLOW_ITEM_FRAME;
        } else if (state instanceof net.minecraft.client.renderer.entity.state.EndCrystalRenderState) {
            // An end crystal is a light source in its own right, and its texture is what gives it its colour, so
            // the glow is kept modest: multiplied by a bright texture, a large value burns the whole thing white.
            emission = Glow.END_CRYSTAL;
            // Emission is the texture colour times a scalar, which burns toward white; a soft purple tint keeps
            // the light it casts violet, like the End.
            COLLECTOR.colorTint(END_CRYSTAL_TINT);
        }
        emission = Math.max(emission, selfLitEmission(minecraft, state));
        if (emission > 0.0f) {
            COLLECTOR.entityEmission(emission);
        }

        try {
            // Built around the entity origin; the renderer places the instance at the entity position.
            minecraft.getEntityRenderDispatcher().submit(state, cameraState, 0.0, 0.0, 0.0, POSE_STACK, COLLECTOR);
        } catch (RuntimeException e) {
            return;
        }

        if (droppedLight != null) COLLECTOR.appendPointItemLight(droppedLight);
        if (COLLECTOR.isEmpty()) {
            return;
        }

        int before = PENDING.size();
        DevProfiler.kind(state);
        addPending(historyId(state), state.x, state.y, state.z, RAY_TRACING_WORLD, still);
        DevProfiler.kind(null);
        if (still) {
            BlockEntityCache.collectedEntity(historyId(state), PENDING.subList(before, PENDING.size()),
                ARENA.addressOf(0L));
        }

        if (state.appearsGlowing()) {
            // Vanilla's glowing effect is an outline of the entity in its team colour, seen through walls. A copy of
            // the entity painted in that colour goes under a mask of its own that only the outline rays in
            // world.rgen look for; they draw the rim wherever a pixel misses the copy but a neighbour hits it.
            COLLECTOR.reset();
            COLLECTOR.colorOverride(state.outlineColor & 0xFFFFFF | 0x010101);
            POSE_STACK.setIdentity();
            try {
                minecraft.getEntityRenderDispatcher().submit(state, cameraState, 0.0, 0.0, 0.0, POSE_STACK, COLLECTOR);
            } catch (RuntimeException e) {
                return;
            }
            if (!COLLECTOR.isEmpty()) {
                addPending(historyId(state) ^ GLOW_OUTLINE_ID_SALT, state.x, state.y, state.z,
                    RAY_TRACING_GLOW_OUTLINE);
            }
        }
    }

    /**
     * The cracks on a block being mined. Minecraft extracts one state per block in progress; each is drawn the way
     * vanilla's breaking pass does it, as the block's own model with the destroy-stage texture projected onto it,
     * placed at the block like any block entity.
     */
    static void collectBlockBreaking(Minecraft minecraft, LevelRenderState levelRenderState) {
        if (levelRenderState.blockBreakingRenderStates.isEmpty()) {
            return;
        }

        List<net.minecraft.client.renderer.block.dispatch.BlockStateModelPart> parts = new ArrayList<>();
        net.minecraft.util.RandomSource random = net.minecraft.util.RandomSource.createThreadLocalInstance();
        for (net.minecraft.client.renderer.state.level.BlockBreakingRenderState state
            : levelRenderState.blockBreakingRenderStates) {
            if (state.blockState().getRenderShape() != net.minecraft.world.level.block.RenderShape.MODEL) {
                continue;
            }

            BlockPos pos = state.blockPos();
            COLLECTOR.reset();
            POSE_STACK.setIdentity();
            POSE_STACK.translate(state.blockState().getOffset(pos));
            net.minecraft.client.renderer.block.dispatch.BlockStateModel model =
                minecraft.getModelManager().getBlockStateModelSet().get(state.blockState());
            random.setSeed(state.blockState().getSeed(pos));
            parts.clear();
            model.collectParts(random, parts);
            COLLECTOR.submitBreakingBlockModel(POSE_STACK, parts, state.progress(), model.hasMaterialFlag(1));
            if (!COLLECTOR.isEmpty()) {
                // Under the particle mask: the cracks sit a hair in front of the block, and as shadow casters they
                // would shade the very face they are drawn on.
                addPending(blockId(pos) ^ BREAKING_ID_SALT, pos.getX(), pos.getY(), pos.getZ(), RAY_TRACING_PARTICLE);
            }
        }
    }

    /**
     * Particles arrive already positioned relative to the camera and facing it. They go in as two objects. Those
     * Minecraft draws at full light whatever is around them (flames, sparks, drops of lava, an end rod's glitter) glow:
     * they are seen bright in the dark and the bounced light picks a little of it up, and they cast no shadow. The
     * others (smoke, dust, falling leaves) are under the world's mask, so they cast the shadow of what they are.
     * Full light alone does not tell them apart: inside a block of light level 15 under the open sky (a campfire, a
     * fire, lava) every particle is at full light, and campfire smoke glowed until it rose out of the campfire's
     * block. So a particle glows only where the block light around it is less than full.
     */
    static void collectParticles(LevelRenderState levelRenderState, CameraRenderState cameraState) {
        COLLECTOR.reset();
        levelRenderState.particlesRenderState.submit(COLLECTOR, cameraState);

        List<PendingLayer> layers = new ArrayList<>();
        List<PendingLayer> shadowLayers = new ArrayList<>();
        Vec3 eye = cameraState.pos;
        net.minecraft.client.multiplayer.ClientLevel level = Minecraft.getInstance().level;
        for (QuadParticleRenderState group : COLLECTOR.drainParticleGroups()) {
            for (SingleQuadParticle.Layer layer : group.layers()) {
                int textureId = TextureTracker.idOf(layer.textureAtlasLocation());
                PBRVertexWriter writer = PARTICLE_WRITER.textureId(textureId)
                    .glintTextureId(0)
                    // Falling leaves, smoke and dust let light through as thin surfaces do.
                    .thin(true)
                    // Translucent ones (smoke, campfire, dust) are kept by their alpha at random, as solid surfaces: traced as
                    // see-through geometry they looked like glass, the world showing through them clearer than in vanilla.
                    .alphaMode(layer.translucent() ? PBRVertexWriter.ALPHA_MODE_STOCHASTIC : PBRVertexWriter.ALPHA_MODE_CUTOUT)
                    .coordinate(NativeGeometry.COORDINATE_WORLD)
                    .albedoEmission(0.0f)
                    .overlayEnabled(false)
                    .computeQuadNormals(true);
                writer.reset();
                group.buildLayer(layer, writer);
                writer.finish();
                if (writer.vertexCount() == 0 || writer.vertexCount() % 4 != 0) {
                    continue;
                }
                // Cut out particles must not be traced as solid geometry; see RenderTypeInfo.geometryType. The
                // no-reflect geometry type looks like the right home for billboards, but its hit group shades them
                // black, so they stay ordinary transparent geometry until that is understood.
                int glowing = writer.bringGlowingQuadsFirst(PARTICLE_GLOW, (x, y, z) -> level == null
                    || level.getBrightness(net.minecraft.world.level.LightLayer.BLOCK,
                        BlockPos.containing(eye.x() + x, eye.y() + y, eye.z() + z)) < 15);
                if (glowing > 0) {
                    layers.add(new PendingLayer(NativeGeometry.GEOMETRY_TYPE_WORLD_TRANSPARENT, textureId, glowing,
                        copyVertices(writer, 0, glowing), "Entity"));
                }
                if (glowing < writer.vertexCount()) {
                    shadowLayers.add(new PendingLayer(NativeGeometry.GEOMETRY_TYPE_WORLD_TRANSPARENT, textureId,
                        writer.vertexCount() - glowing, copyVertices(writer, glowing, writer.vertexCount() - glowing),
                        "Entity"));
                }
            }
        }

        Vec3 camera = cameraState.pos;
        if (!layers.isEmpty()) {
            PENDING.add(new PendingEntity(PARTICLES_ID, camera.x(), camera.y(), camera.z(), RAY_TRACING_PARTICLE,
                layers));
        }
        if (!shadowLayers.isEmpty()) {
            PENDING.add(new PendingEntity(SHADOW_PARTICLES_ID, camera.x(), camera.y(), camera.z(), RAY_TRACING_WORLD,
                shadowLayers));
        }
    }

    /** Writers for other mods' geometry (RadianteGeometry), reused from frame to frame. */
    static final List<PBRVertexWriter> EXTERNAL_WRITERS = new ArrayList<>();

    /**
     * Geometry other mods hand over through {@link com.g2806.radiante.api.RadianteGeometry}: what they would have
     * drawn in level passes that do not run while the world is traced. Each provider becomes one object placed at
     * the camera, a layer per texture and alpha mode, built anew every frame as particles are - but under the world
     * mask, so it casts shadows and shows in reflections.
     */
    static void collectExternal(LevelRenderState levelRenderState, CameraRenderState cameraState) {
        Vec3 camera = cameraState.pos;
        // Mods that submit as they would to Minecraft's own collector: camera-relative, so the object sits at the
        // camera.
        for (Map.Entry<String, com.g2806.radiante.api.RadianteGeometry.RegisteredSubmitter> entry
            : com.g2806.radiante.api.RadianteGeometry.submitters().entrySet()) {
            COLLECTOR.reset();
            try {
                entry.getValue().submitter().submit(COLLECTOR);
            } catch (RuntimeException e) {
                continue;
            }
            if (!COLLECTOR.isEmpty()) {
                addPending(entry.getKey().hashCode() ^ EXTERNAL_ID_SALT, camera.x(), camera.y(), camera.z(),
                    entry.getValue().castsShadows() ? RAY_TRACING_WORLD : RAY_TRACING_PARTICLE);
            }
        }

        Map<String, com.g2806.radiante.api.RadianteGeometry.RegisteredProvider> providers =
            com.g2806.radiante.api.RadianteGeometry.providers();
        if (providers.isEmpty()) {
            return;
        }
        float partialTick = levelRenderState.worldPartialTicks;
        int[] used = {0};
        for (Map.Entry<String, com.g2806.radiante.api.RadianteGeometry.RegisteredProvider> provider
            : providers.entrySet()) {
            // Texture id and alpha mode, to the writer and the geometry type of its layer.
            Map<Long, PBRVertexWriter> writers = new java.util.LinkedHashMap<>();
            Map<Long, Integer> geometryTypes = new java.util.HashMap<>();
            com.g2806.radiante.api.RadianteGeometry.Sink sink = new com.g2806.radiante.api.RadianteGeometry.Sink() {
                @Override
                public Vec3 camera() {
                    return camera;
                }

                @Override
                public float partialTick() {
                    return partialTick;
                }

                @Override
                public com.mojang.blaze3d.vertex.VertexConsumer quads(com.mojang.renderpearl.api.textures.GpuTexture texture,
                    com.g2806.radiante.api.RadianteGeometry.Alpha alpha) {
                    return writer(TextureTracker.idOf(texture), alpha);
                }

                @Override
                public com.mojang.blaze3d.vertex.VertexConsumer quads(net.minecraft.resources.Identifier texture,
                    com.g2806.radiante.api.RadianteGeometry.Alpha alpha) {
                    return writer(TextureTracker.idOf(texture), alpha);
                }

                private PBRVertexWriter writer(int textureId, com.g2806.radiante.api.RadianteGeometry.Alpha alpha) {
                    long key = (long) textureId << 2 | alpha.ordinal();
                    PBRVertexWriter writer = writers.get(key);
                    if (writer != null) {
                        return writer;
                    }
                    if (used[0] == EXTERNAL_WRITERS.size()) {
                        EXTERNAL_WRITERS.add(new PBRVertexWriter(1024));
                    }
                    writer = EXTERNAL_WRITERS.get(used[0]++);
                    writer.reset();
                    writer.textureId(textureId)
                        .glintTextureId(0)
                        .alphaMode(switch (alpha) {
                            case OPAQUE -> PBRVertexWriter.ALPHA_MODE_OPAQUE;
                            case CUTOUT -> PBRVertexWriter.ALPHA_MODE_CUTOUT;
                            case TRANSLUCENT -> PBRVertexWriter.ALPHA_MODE_TRANSPARENT;
                            case DITHERED -> PBRVertexWriter.ALPHA_MODE_STOCHASTIC;
                        })
                        .coordinate(NativeGeometry.COORDINATE_WORLD)
                        .albedoEmission(0.0f)
                        .overlayEnabled(false)
                        .computeQuadNormals(true);
                    writers.put(key, writer);
                    // Only what is opaque all over may be solid geometry; see RenderTypeInfo.geometryType.
                    geometryTypes.put(key, alpha == com.g2806.radiante.api.RadianteGeometry.Alpha.OPAQUE
                        ? NativeGeometry.GEOMETRY_TYPE_WORLD_SOLID : NativeGeometry.GEOMETRY_TYPE_WORLD_TRANSPARENT);
                    return writer;
                }
            };
            try {
                provider.getValue().provider().provide(sink);
            } catch (RuntimeException e) {
                // Another mod's mistake costs its own geometry for the frame, not the frame.
                continue;
            }

            List<PendingLayer> layers = new ArrayList<>();
            for (Map.Entry<Long, PBRVertexWriter> entry : writers.entrySet()) {
                PBRVertexWriter writer = entry.getValue();
                writer.finish();
                if (writer.vertexCount() == 0 || writer.vertexCount() % 4 != 0) {
                    continue;
                }
                layers.add(new PendingLayer(geometryTypes.get(entry.getKey()), (int) (entry.getKey() >> 2),
                    writer.vertexCount(), copyVertices(writer), "Entity"));
            }
            if (!layers.isEmpty()) {
                PENDING.add(new PendingEntity(provider.getKey().hashCode() ^ EXTERNAL_ID_SALT, camera.x(), camera.y(),
                    camera.z(), provider.getValue().castsShadows() ? RAY_TRACING_WORLD : RAY_TRACING_PARTICLE, layers));
            }
        }
    }

    static final int EXTERNAL_ID_SALT = 0x65787467;

    /**
     * The player, in first person. Minecraft draws nothing of them then - no model is extracted for the camera
     * entity - so the world around the player had no shadow of them in it and they were missing from every
     * reflection. Their model is submitted here under the mask camera rays skip and shadow rays and later bounces
     * keep. Off with the "First Person Shadow" option.
     */
    static void collectPlayerShadow(Minecraft minecraft, LevelRenderState levelRenderState,
        CameraRenderState cameraState) {
        PlayerRenderState playerState = levelRenderState.playerRenderState;
        if (!Options.firstPersonShadow || !playerState.hasPlayer || playerState.avatarRenderState == null
            || !minecraft.options.getCameraType().isFirstPerson() || cameraState.entityRenderState.isSleeping
            || minecraft.gameMode == null || minecraft.gameMode.getPlayerMode() == GameType.SPECTATOR) {
            return;
        }

        EntityRenderState state = playerState.avatarRenderState;
        COLLECTOR.reset();
        configureHeldLights(minecraft, COLLECTOR, state);
        COLLECTOR.skipNameTags();
        POSE_STACK.setIdentity();

        try {
            minecraft.getEntityRenderDispatcher().submit(state, cameraState, 0.0, 0.0, 0.0, POSE_STACK, COLLECTOR);
        } catch (RuntimeException e) {
            return;
        }

        if (COLLECTOR.isEmpty()) {
            return;
        }

        addPending(PLAYER_SHADOW_ID, state.x, state.y, state.z, RAY_TRACING_PLAYER);
    }

    /** Glass blocks and panes, as items: drawn as glass when held (see EntityCollector.heldGlass). */
    static boolean isGlassItem(net.minecraft.world.item.ItemStack stack) {
        return stack.getItem() instanceof net.minecraft.world.item.BlockItem item
            && BlockKinds.isGlass(item.getBlock().defaultBlockState());
    }

    /**
     * The id of whatever stands at a block. {@code BlockPos.hashCode} will not do: it gives two blocks 31 apart
     * along x and one apart in height the same number, and the renderer keeps what it has built by id - two signs
     * sharing one took each other's place every frame and were both built again every frame, by the hundred
     * around a server spawn.
     */
    static int blockId(BlockPos pos) {
        long mixed = pos.asLong() * 0x9E3779B97F4A7C15L;
        return (int) (mixed ^ mixed >>> 32);
    }

    /**
     * The id an entity's geometry is matched to the previous frame's by, for its motion vectors. Minecraft makes a
     * new render state every frame, so keyed by the state object nothing ever matched: every moving mob and player
     * was taken for a still one, and the upscaler, finding it elsewhere than the motion vectors said, threw its
     * history away and showed it noisy.
     */
    static int historyId(EntityRenderState state) {
        int entityId = ((EntityIdHolder) state).radiante$entityId();
        return entityId == Integer.MIN_VALUE ? System.identityHashCode(state) : entityId * 0x9E3779B1 ^ 0x7F4A7C15;
    }

    /** The text instance of a block entity (see addPending), as against a name tag, which is never kept. */
    static boolean isBlockText(PendingEntity entity) {
        return entity.rayTracingFlag() == RAY_TRACING_PARTICLE && entity.prebuiltBlas() != PREBUILT_BLAS_NONE
            && !entity.layers().isEmpty() && entity.layers().get(0).name().startsWith("text");
    }

    static void addPending(int id, double x, double y, double z, int rayTracingFlag) {
        addPending(id, x, y, z, rayTracingFlag, false);
    }

    static void addPending(int id, double x, double y, double z, int rayTracingFlag, boolean cacheable) {
        addPending(COLLECTOR, id, x, y, z, rayTracingFlag, cacheable);
    }

    static void addPending(EntityCollector collector, int id, double x, double y, double z,
        int rayTracingFlag, boolean cacheable) {
        // An entity whose origin is not a real point in the world - a block-attached one that lost its support
        // block reports exactly that - would place its whole model outside anything the tracer can build.
        if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z)) {
            return;
        }

        List<PendingLayer> layers = new ArrayList<>();
        List<PendingLayer> nameTagLayers = new ArrayList<>();
        int profiledVertices = 0;
        for (Map.Entry<RenderType, PBRVertexWriter> entry : collector.layers().entrySet()) {
            PBRVertexWriter writer = entry.getValue();
            writer.finish();
            if (writer.vertexCount() == 0 || writer.vertexCount() % 4 != 0) {
                RenderTypeInfo dropped = RenderTypeInfo.of(entry.getKey());
                if (Options.debugLogging && DEBUG_TEXT_LAYERS < 5 && dropped.needsComputedNormals()) {
                    DEBUG_TEXT_LAYERS++;
                    RadianteRenderer.LOGGER.info("layer dropped: group={} vertices={}", dropped.groupName(),
                        writer.vertexCount());
                }
                continue;
            }

            RenderTypeInfo info = RenderTypeInfo.of(entry.getKey());
            if (Options.debugLogging && DEBUG_TEXT_LAYERS < 5 && info.needsComputedNormals()) {
                DEBUG_TEXT_LAYERS++;
                RadianteRenderer.LOGGER.info("layer accepted: group={} geometry={} textureId={} vertices={}",
                    info.groupName(), info.geometryType(), info.textureId(), writer.vertexCount());
            }
            PendingLayer layer = new PendingLayer(info.geometryType(), info.textureId(), writer.vertexCount(),
                copyVertices(writer), info.groupName());
            profiledVertices += writer.vertexCount();
            // Text casts no shadow, as in vanilla: the letters of a sign stand a hair off its board, and their
            // shadow on it read as a second, darker line of text behind the first.
            boolean overlay = collector.isNameTagLayer(entry.getKey()) || info.groupName().startsWith("text");
            (overlay ? nameTagLayers : layers).add(layer);
        }
        DevProfiler.count(layers.size() + nameTagLayers.size(), profiledVertices);

        if (!layers.isEmpty()) {
            PENDING.add(new PendingEntity(id, x, y, z, rayTracingFlag, layers, cacheable));
        }
        // Name tags go in as their own instance under the particle mask: seen by camera rays, skipped by shadow
        // rays, so neither the letters nor the plate behind them cast a shadow on the world.
        if (!nameTagLayers.isEmpty() && rayTracingFlag != RAY_TRACING_GLOW_OUTLINE) {
            PENDING.add(new PendingEntity(id ^ NAME_TAG_ID_SALT, x, y, z, RAY_TRACING_PARTICLE, nameTagLayers,
                cacheable));
        }
    }

}
