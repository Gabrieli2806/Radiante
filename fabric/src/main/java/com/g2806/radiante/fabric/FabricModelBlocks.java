package com.g2806.radiante.fabric;

import com.g2806.radiante.client.RadianteClient;
import com.mojang.blaze3d.vertex.QuadInstance;
import net.fabricmc.fabric.api.client.renderer.v1.Renderer;
import net.fabricmc.fabric.api.client.renderer.v1.mesh.MutableQuadView;
import net.fabricmc.fabric.api.client.renderer.v1.mesh.QuadEmitter;
import net.fabricmc.fabric.api.client.renderer.v1.render.AltModelBlockRenderer;
import net.fabricmc.fabric.api.client.renderer.v1.sprite.FabricTextureAtlas;
import net.fabricmc.fabric.api.client.renderer.v1.sprite.SpriteFinder;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.block.BlockAndTintGetter;
import net.minecraft.client.renderer.block.BlockQuadOutput;
import net.minecraft.client.renderer.block.dispatch.BlockStateModel;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.state.BlockState;

/**
 * Blocks whose model draws itself through Fabric's rendering API: connected textures (Continuity), models a mod
 * makes at run time. Minecraft builds sections through SectionCompiler, which Fabric's API turns towards these
 * models; Radiante builds its own sections, and vanilla's ModelBlockRenderer alone saw only the quads such a model
 * keeps for its item, or none, so the blocks came out plain or missing. They go through Fabric's renderer here, and
 * each quad it gives back, tinted and lit, is handed on as the vanilla renderer's would be.
 *
 * <p>Vanilla's own models keep vanilla's renderer, which is quicker: only a model class from outside Minecraft takes
 * this way. A model that fails here falls back to vanilla's renderer, logged once.
 */
final class FabricModelBlocks {

    private static final ClassValue<Boolean> MODDED = new ClassValue<>() {
        @Override
        protected Boolean computeValue(Class<?> type) {
            return !type.getName().startsWith("net.minecraft.");
        }
    };

    /** One per section build thread: Fabric's renderer, its emitter, and where this block's quads go. */
    private static final class Context {
        final AltModelBlockRenderer renderer;
        final QuadEmitter emitter;
        final QuadInstance instance = new QuadInstance();
        SpriteFinder sprites;
        BlockQuadOutput output;
        float x;
        float y;
        float z;

        Context() {
            Minecraft minecraft = Minecraft.getInstance();
            // As ChunkManager's vanilla renderer: no ambient occlusion (the tracer shades), faces culled.
            this.renderer = Renderer.get().altModelBlockRenderer(false, true, minecraft.getBlockColors());
            this.emitter = Renderer.get().quadEmitter(this::accept);
        }

        private void accept(MutableQuadView quad) {
            TextureAtlasSprite sprite = this.sprites.find(quad);
            for (int i = 0; i < 4; i++) {
                this.instance.setColor(i, quad.color(i));
                this.instance.setLightCoords(i, quad.lightmap(i));
            }
            this.output.put(this.x, this.y, this.z, quad.toBakedQuad(sprite), this.instance);
        }
    }

    private static final ThreadLocal<Context> CONTEXT = ThreadLocal.withInitial(Context::new);
    private static volatile boolean warned;

    private FabricModelBlocks() {
    }

    static boolean tesselate(BlockQuadOutput output, float x, float y, float z, BlockAndTintGetter level,
                             BlockPos pos, BlockState state, BlockStateModel model, long seed) {
        if (!MODDED.get(model.getClass())) {
            return false;
        }
        try {
            Context context = CONTEXT.get();
            context.sprites = blockSprites();
            context.output = output;
            context.x = x;
            context.y = y;
            context.z = z;
            context.renderer.tesselateBlock(context.emitter, 0.0f, 0.0f, 0.0f, level, pos, state, model, seed);
            return true;
        } catch (RuntimeException | LinkageError e) {
            if (!warned) {
                warned = true;
                RadianteClient.LOGGER.warn("A block model drawn through Fabric's rendering API ({}) could not be built"
                    + " that way; such blocks are built as vanilla models instead", model.getClass().getName(), e);
            }
            return false;
        }
    }

    private static SpriteFinder blockSprites() {
        TextureAtlas atlas = (TextureAtlas) Minecraft.getInstance().getTextureManager()
            .getTexture(TextureAtlas.LOCATION_BLOCKS);
        return ((FabricTextureAtlas) atlas).spriteFinder();
    }
}
