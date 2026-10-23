package com.g2806.radiante.client.render;

import com.g2806.radiante.client.proxy.vulkan.TextureProxy;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import net.minecraft.client.Minecraft;
import net.minecraft.data.AtlasIds;
import net.minecraft.client.renderer.block.dispatch.BlockStateModel;
import net.minecraft.client.renderer.block.dispatch.BlockStateModelPart;
import net.minecraft.client.renderer.texture.SpriteContents;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.resources.model.geometry.BakedQuad;
import net.minecraft.core.Direction;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;
import org.lwjgl.system.MemoryUtil;

/**
 * Makes light emitting blocks glow and light up their surroundings. Their bright texels are written into a LabPBR
 * specular atlas (emission in alpha) mapped onto the block atlas, and registered as emitter cells the renderer
 * samples as light sources. Vanilla textures have no emission maps: a block's light level decides how strong
 * it is, and each part of the texture emits by its own brightness, so a torch stick stays dark while the flame glows.
 */
public final class EmissionTiles {

    /** Cells per sprite side; small enough for 16px textures to keep flames separate from their stick. */
    private static final int CELLS_PER_SIDE = 4;
    /** Texels whose brightest channel is below this do not emit. */
    private static final float MIN_BRIGHTNESS = 0.55f;
    /** Floor for textures that never reach MIN_BRIGHTNESS anywhere. */
    private static final float DARK_EMITTER_MIN_BRIGHTNESS = 0.15f;
    /** For those dark emitters, a texel emits when it is this bright relative to the brightest texel. */
    private static final float RELATIVE_BRIGHTNESS = 0.6f;
    /**
     * And for every other texture, how bright a texel has to be next to the brightest one in the same sprite. A
     * torch is the reason this exists: its flame sits at full brightness while its wooden stick reaches 0.62, above
     * the absolute floor, so on that alone the whole torch glowed - stick included.
     */
    private static final float BRIGHT_EMITTER_RELATIVE = 0.65f;
    /** Texels at least this saturated count as the coloured, glowing part of a texture. */
    private static final float MIN_GLOW_SATURATION = 0.35f;
    /** For lights with a white core, how bright a texel has to be next to the brightest one to glow. */
    private static final float WHITE_CORE_RELATIVE = 0.85f;
    private static final int CELL_BYTES = 8 * Float.BYTES;
    /**
     * Light a fully lit face of a light level 15 block casts on its surroundings. A block's radiance is divided by
     * how much of its texture glows, so the light it casts depends on its light level and not on the size of its
     * glowing part: a torch, whose flame is a few texels, lights a room like a torch held in the hand (HeldLight),
     * instead of about a hundred times less. Tuned so the torch's four sides and top add up to the held light.
     */
    private static final float LIGHT_PER_FACE = 4.8f;
    /** Floor on the glowing share of a texture, so a sprite with one lit texel does not become a searchlight. */
    private static final float MIN_GLOW_COVERAGE = 0.01f;

    /** Renderer id of the generated LabPBR specular atlas that carries per-texel emission, or -1. */
    private static int specularTextureId = -1;
    private static int specularWidth;
    private static int specularHeight;
    private static int specularAtlasId = -1;
    /**
     * An end portal frame has a light level of 1 in every state, so vanilla would light its frame whether an eye
     * sits in it or not. Only the eye glows here: its sprite is drawn by the filled frame alone, so the light
     * comes on when an eye is placed.
     */
    private static final Identifier END_PORTAL_EYE = Identifier.withDefaultNamespace("block/end_portal_frame_eye");
    // The light a lit eye casts: enough for the frame it sits in and the floor around the ring.
    private static final int END_PORTAL_EYE_EMISSION = 8;
    // How brightly the eye itself shows, of the most a texture can: all of it. The eye is a dark teal, and at the
    // strength of the light it casts it showed dimmer than the pale frame it lit - the light did not seem to come
    // from it.
    private static final float END_PORTAL_EYE_GLOW = 1.0f;
    private static final Identifier[] END_PORTAL_FRAME_SPRITES = {
        Identifier.withDefaultNamespace("block/end_portal_frame_top"),
        Identifier.withDefaultNamespace("block/end_portal_frame_side"),
    };

    private static final Identifier[] LAVA_SPRITES = {
        Identifier.withDefaultNamespace("block/lava_still"),
        Identifier.withDefaultNamespace("block/lava_flow"),
    };

    /**
     * Blocks Radiante lights that vanilla leaves dark. A block of redstone carries no light level at all in
     * Minecraft, but it reads as a solid block of glowing mineral and Bedrock's ray traced mode treats it as one,
     * so it gets a low level of its own here. Kept deliberately dim: it is decoration, not a torch.
     */
    private static final Map<Block, Integer> EXTRA_EMISSION = Map.of(
        net.minecraft.world.level.block.Blocks.REDSTONE_BLOCK, 5);

    private static Object registeredFor;
    /** Whether the last registration took emitters from Bedrock MER maps; toggling it registers again. */
    private static boolean registeredWithMer;
    /** Light level of each emissive sprite of the last registration, for {@link #uploadFromMer}. */
    private static Map<TextureAtlasSprite, Integer> lightLevels = Map.of();
    /**
     * The states of a light block that give no light although they draw the very sprites its lit states glow with:
     * redstone ore is one texture lit or not. Their quads are marked (PBRVertexWriter.unlit) and neither glow nor
     * cast light; before, every redstone ore in a cave lit the stone around it red, stepped on or not.
     */
    private static volatile Set<BlockState> unlitStates = Set.of();

    private EmissionTiles() {
    }

    public static void invalidate() {
        registeredFor = null;
        PbrAtlases.invalidate();
    }

    /** Whether this state draws a light block's glowing sprites while giving no light; see {@link #unlitStates}. */
    public static boolean isUnlit(BlockState state) {
        return unlitStates.contains(state);
    }

    /** Registers the emitters once per block atlas; call on the render thread once models are loaded. */
    public static void registerIfNeeded(Minecraft minecraft) {
        TextureAtlas atlas = minecraft.getAtlasManager().getAtlasOrThrow(AtlasIds.BLOCKS);
        Object key = TextureTracker.gpuTextureOrNull(atlas);
        boolean withMer = com.g2806.radiante.client.pipeline.Pipeline.castsBedrockGlow();
        if (key == null || (key == registeredFor && withMer == registeredWithMer)) {
            return;
        }

        int atlasId = TextureTracker.idOf(atlas.getTexture());
        if (atlasId == 0) {
            return;
        }
        registeredFor = key;
        registeredWithMer = withMer;

        Map<TextureAtlasSprite, Integer> emitters = collectEmissiveSprites(minecraft, atlas);
        lightLevels = emitters;
        int atlasWidth = atlas.getTexture().getWidth(0);
        int atlasHeight = atlas.getTexture().getHeight(0);
        int mipLevels = atlas.getTexture().getMipLevels();
        // The emission worked out from the albedo is written first and the authored maps go over it, so a pack
        // that covers some blocks and not others - Radiante's own built in maps are exactly that - leaves the rest
        // glowing instead of turning the derived emission off everywhere. A lit redstone lamp has no built in map
        // and was dark for that reason.
        ByteBuffer specular = MemoryUtil.memCalloc(atlasWidth * atlasHeight * 4);
        boolean packMaps = false;
        try {
            for (Map.Entry<TextureAtlasSprite, Integer> entry : emitters.entrySet()) {
                upload(atlasId, entry.getKey(), entry.getValue(), atlasWidth, atlasHeight, specular);
            }
            packMaps = PbrAtlases.build(minecraft, atlas, atlasId, specular) > 0;
            if (!packMaps) {
                uploadSpecularAtlas(atlasId, specular, atlasWidth, atlasHeight, mipLevels);
            }
        } finally {
            MemoryUtil.memFree(specular);
        }

        if (com.g2806.radiante.client.option.Options.debugLogging) {
            RadianteRenderer.LOGGER.info("Registered {} emissive block textures, {} unlit block states",
                emitters.size(), unlitStates.size());
        }
        ChunkManager.markAllDirty();
    }

    private static Map<TextureAtlasSprite, Integer> collectEmissiveSprites(Minecraft minecraft, TextureAtlas atlas) {
        Map<TextureAtlasSprite, Integer> emitters = new IdentityHashMap<>();
        // Textures are shared between blocks: a beacon is a light source and its model is built out of the ordinary
        // glass texture. Marking every sprite of an emissive block's model as emissive would light up every pane of
        // glass in the world, so a sprite that any block without light emission also uses is left alone.
        Set<TextureAtlasSprite> sharedWithDarkBlocks = Collections.newSetFromMap(new IdentityHashMap<>());
        RandomSource random = RandomSource.create(42L);
        List<BlockStateModelPart> parts = new ArrayList<>();
        // What each dark state of a light block draws, to find the ones drawing a sprite that glows (see unlitStates).
        Map<BlockState, Set<TextureAtlasSprite>> darkStateSprites = new IdentityHashMap<>();

        for (Block block : BuiltInRegistries.BLOCK) {
            // Redstone ore is the same block and the same texture whether it is lit or not, and only the lit state
            // carries a light level. Blacklisting on the dark state alone therefore threw away the very sprite it
            // was meant to light, so what disqualifies a sprite is a block that emits nothing in any of its states.
            boolean emitsSomewhere = false;
            for (BlockState state : block.getStateDefinition().getPossibleStates()) {
                if (state.getLightEmission() > 0) {
                    emitsSomewhere = true;
                    break;
                }
            }

            int extra = EXTRA_EMISSION.getOrDefault(block, 0);
            // The sprites of this block's lit states and of its dark ones. A lit furnace, smoker, blast furnace or
            // campfire draws one sprite of its own (the glowing front, the burning logs) next to sprites its unlit
            // state draws too (the sides and top, the plain logs): those stay dark, or the whole block glowed on
            // every face. A block whose lit and dark states draw the same sprites (redstone ore) keeps them all.
            Map<TextureAtlasSprite, Integer> litSprites = new IdentityHashMap<>();
            Set<TextureAtlasSprite> darkSprites = Collections.newSetFromMap(new IdentityHashMap<>());
            for (BlockState state : block.getStateDefinition().getPossibleStates()) {
                int level = Math.max(state.getLightEmission(), extra);
                BlockStateModel model = minecraft.getModelManager().getBlockStateModelSet().get(state);
                parts.clear();
                random.setSeed(42L);
                model.collectParts(random, parts);
                Set<TextureAtlasSprite> drawn = level == 0 && emitsSomewhere
                    ? Collections.newSetFromMap(new IdentityHashMap<>()) : null;
                for (BlockStateModelPart part : parts) {
                    for (Direction direction : Direction.values()) {
                        addSprites(part.getQuads(direction), level, emitsSomewhere || extra > 0, litSprites,
                            sharedWithDarkBlocks, darkSprites, drawn);
                    }
                    addSprites(part.getQuads(null), level, emitsSomewhere || extra > 0, litSprites,
                        sharedWithDarkBlocks, darkSprites, drawn);
                }
                if (drawn != null && !drawn.isEmpty()) {
                    darkStateSprites.put(state, drawn);
                }
            }
            if (!darkSprites.isEmpty() && !darkSprites.containsAll(litSprites.keySet())) {
                litSprites.keySet().removeAll(darkSprites);
            }
            for (Map.Entry<TextureAtlasSprite, Integer> lit : litSprites.entrySet()) {
                emitters.merge(lit.getKey(), lit.getValue(), Math::max);
            }
        }

        emitters.keySet().removeAll(sharedWithDarkBlocks);

        for (Identifier id : END_PORTAL_FRAME_SPRITES) {
            TextureAtlasSprite sprite = atlas.getSprite(id);
            if (sprite != null) {
                emitters.remove(sprite);
            }
        }
        TextureAtlasSprite eye = atlas.getSprite(END_PORTAL_EYE);
        if (eye != null) {
            emitters.merge(eye, END_PORTAL_EYE_EMISSION, Math::max);
        }

        // Lava is drawn by the fluid renderer and has no block model.
        for (Identifier id : LAVA_SPRITES) {
            TextureAtlasSprite sprite = atlas.getSprite(id);
            if (sprite != null) {
                emitters.merge(sprite, 15, Math::max);
            }
        }

        Set<BlockState> unlit = Collections.newSetFromMap(new IdentityHashMap<>());
        for (Map.Entry<BlockState, Set<TextureAtlasSprite>> dark : darkStateSprites.entrySet()) {
            for (TextureAtlasSprite sprite : dark.getValue()) {
                if (emitters.containsKey(sprite)) {
                    unlit.add(dark.getKey());
                    break;
                }
            }
        }
        unlitStates = Collections.unmodifiableSet(unlit);
        return emitters;
    }

    private static void addSprites(List<BakedQuad> quads, int level, boolean blockEmitsSomewhere,
        Map<TextureAtlasSprite, Integer> emitters, Set<TextureAtlasSprite> sharedWithDarkBlocks,
        Set<TextureAtlasSprite> darkSprites, Set<TextureAtlasSprite> drawn) {
        for (BakedQuad quad : quads) {
            TextureAtlasSprite sprite = quad.materialInfo().sprite();
            if (drawn != null) {
                drawn.add(sprite);
            }
            if (level > 0) {
                emitters.merge(sprite, level, Math::max);
            } else if (!blockEmitsSomewhere) {
                sharedWithDarkBlocks.add(sprite);
            } else {
                darkSprites.add(sprite);
            }
        }
    }

    private static void upload(int atlasId, TextureAtlasSprite sprite, int level, int atlasWidth, int atlasHeight,
        ByteBuffer specular) {
        SpriteContents contents = sprite.contents();
        ByteBuffer[] mips = ((SpriteContentsAccess) contents).radiante$mipImages();
        if (mips.length == 0 || mips[0] == null) {
            return;
        }

        ByteBuffer pixels = mips[0];
        int width = contents.width();
        int height = contents.height();
        int rowPixels = Math.max(1, ((SpriteContentsAccess) contents).radiante$mipWidth(0));
        float strength = level / 15.0f;
        // Brightness is the strongest channel, not perceived luminance: pure red has a luminance of 0.21, so a
        // luminance cut skips the red of redstone and the orange of magma, and lets only the palest specks of lava
        // through. Textures that glow in colour also carry pale highlights and grey stone, so when a texture has
        // bright, saturated texels only those emit; the white glints on redstone ore stay dark. Textures that never
        // get bright anywhere, a nether portal glowing all over in dark purple, fall back to a relative cut.
        float brightest = maxBrightness(pixels, width, height, rowPixels);
        float threshold = brightest >= MIN_BRIGHTNESS
            ? Math.max(MIN_BRIGHTNESS, brightest * BRIGHT_EMITTER_RELATIVE)
            : Math.max(DARK_EMITTER_MIN_BRIGHTNESS, brightest * RELATIVE_BRIGHTNESS);
        boolean coloured = hasSaturatedTexels(pixels, width, height, rowPixels, threshold);
        // Lava glows all over at full strength, dark crust included; cutting it to its brightest texels, or
        // weighting texels by brightness, left it a dull grey-brown instead of the molten orange it should be.
        // A light whose core is white or pale (the copper lantern's green-white flame) inside a brightly coloured
        // body (the lantern's copper frame): the saturation rule lit the body and left the core dark. Only its
        // brightest texels glow instead.
        if (hasWhiteCore(sprite)) {
            threshold = Math.max(threshold, brightest * WHITE_CORE_RELATIVE);
            coloured = false;
        }
        boolean eye = sprite.contents().name().equals(END_PORTAL_EYE);
        // Under the Bedrock profile the eye glows like any other light of its level (no pack map) or as its pack
        // authors it (see keepSetGlow): Bedrock RTX shows an eye of ender in its frame barely glowing, and the full
        // glow lit a ring of frames brighter than any lamp around it.
        boolean bedrockGlow = com.g2806.radiante.client.pipeline.Pipeline.castsBedrockGlow();
        boolean lava = isLava(sprite);
        // Each texel by its yellow (green channel): the dark orange glows least and the bright swirls most, so the
        // surface keeps vanilla's flowing pattern; all at full strength it was overexposed into a pale, flat peach.
        if (lava) {
            threshold = 0.0f;
            coloured = false;
        }
        // The beacon's core is light all over, cyan rim and white centre alike, each texel by its brightness: with
        // only its brightest texels lit the centre glowed and the rim and corners around it sat dark. So is an eye
        // of ender in its frame: the whole eye glows, not the two specks of its highlight.
        if (glowsAllOver(sprite)) {
            threshold = 0.0f;
            coloured = false;
        }
        int cellWidth = Math.max(1, width / CELLS_PER_SIDE);
        int cellHeight = Math.max(1, height / CELLS_PER_SIDE);

        List<float[]> cells = new ArrayList<>();
        float spriteEmission = 0.0f;
        for (int cellY = 0; cellY < height; cellY += cellHeight) {
            for (int cellX = 0; cellX < width; cellX += cellWidth) {
                float emission = 0.0f;
                float red = 0.0f;
                float green = 0.0f;
                float blue = 0.0f;
                int texels = 0;
                for (int y = cellY; y < Math.min(height, cellY + cellHeight); y++) {
                    for (int x = cellX; x < Math.min(width, cellX + cellWidth); x++) {
                        int index = (y * rowPixels + x) * 4;
                        if (index + 3 >= pixels.capacity()) {
                            continue;
                        }
                        float r = (pixels.get(index) & 0xFF) / 255.0f;
                        float g = (pixels.get(index + 1) & 0xFF) / 255.0f;
                        float b = (pixels.get(index + 2) & 0xFF) / 255.0f;
                        float a = (pixels.get(index + 3) & 0xFF) / 255.0f;
                        texels++;
                        float brightness = Math.max(r, Math.max(g, b));
                        if (a < 0.1f || brightness < threshold
                            || (coloured && saturation(r, g, b) < MIN_GLOW_SATURATION)) {
                            continue;
                        }
                        float weight = a * brightness;
                        if (specular != null) {
                            writeSpecularEmission(specular, atlasWidth, atlasHeight,
                                Math.round(sprite.getU0() * atlasWidth) + x,
                                Math.round(sprite.getV0() * atlasHeight) + y,
                                eye && !bedrockGlow ? END_PORTAL_EYE_GLOW
                                    : lava ? strength * LAVA_SURFACE_GLOW * g : strength * brightness);
                        }
                        emission += weight;
                        red += r * weight;
                        green += g * weight;
                        blue += b * weight;
                    }
                }

                if (emission <= 0.0f || texels == 0) {
                    continue;
                }
                spriteEmission += emission;

                float u0 = (sprite.getU0() * atlasWidth + cellX) / atlasWidth;
                float v0 = (sprite.getV0() * atlasHeight + cellY) / atlasHeight;
                float u1 = (sprite.getU0() * atlasWidth + Math.min(width, cellX + cellWidth)) / atlasWidth;
                float v1 = (sprite.getV0() * atlasHeight + Math.min(height, cellY + cellHeight)) / atlasHeight;
                // Lava's bright texels are near white, and the light they cast burnt what it fell on to a pale cream:
                // lava lights its surroundings red-orange, the more so the yellower the texel.
                float greenShare = lava ? LAVA_LIGHT_GREEN : 1.0f;
                float blueShare = lava ? LAVA_LIGHT_BLUE : 1.0f;
                cells.add(new float[] {u0, v0, u1, v1, LIGHT_PER_FACE * strength * emission / texels, red / emission,
                    green / emission * greenShare,
                    blue / emission * blueShare});
            }
        }

        if (cells.isEmpty()) {
            return;
        }
        float coverage = Math.max(MIN_GLOW_COVERAGE, spriteEmission / (float) (width * height));
        // Lava lies in seas, every block of them a full light: blocks beside it were lit burning orange.
        float scale = lava ? LAVA_CAST : 1.0f;
        for (float[] cell : cells) {
            cell[4] *= scale / coverage;
        }

        long address = MemoryUtil.nmemAllocChecked((long) cells.size() * CELL_BYTES);
        try {
            for (int i = 0; i < cells.size(); i++) {
                float[] cell = cells.get(i);
                for (int j = 0; j < cell.length; j++) {
                    MemoryUtil.memPutFloat(address + (long) i * CELL_BYTES + (long) j * Float.BYTES, cell[j]);
                }
            }
            long tileKey = ((long) sprite.getX() << 32) ^ sprite.getY() ^ ((long) width << 48) ^ height;
            TextureProxy.uploadEmissionTile(atlasId, tileKey, address, cells.size());
        } finally {
            MemoryUtil.nmemFree(address);
        }
    }

    /**
     * Re-registers a light emitting block's cells from its Bedrock MER map (see PbrAtlases), for the experimental
     * Bedrock profile, which casts light as Bedrock RTX does: a glowing texel lights its surroundings with exactly
     * the light it shows, its colour times MER green squared (times 5 and the emissive multiplier, in the shaders).
     * Each cell holds the mean of that over its texels, the dark ones included, and is marked exact (a negative
     * emission, see emission.cpp) so the shaders take it at that scale and not at a light level's. Before, how much
     * light a block cast followed its light level: a block whose pack gives it little glow lit the room around it
     * without seeming to glow, and the brightest lights of a pack cast no more than any other light of their level.
     * A block whose MER marks nothing emissive casts nothing, as in Bedrock.
     */
    static void uploadFromMer(int atlasId, TextureAtlasSprite sprite, ByteBuffer mer, int atlasWidth, int atlasHeight) {
        Integer level = lightLevels.get(sprite);
        if (level == null || level <= 0 || !com.g2806.radiante.client.pipeline.Pipeline.castsBedrockGlow()) {
            return;
        }
        SpriteContents contents = sprite.contents();
        ByteBuffer[] mips = ((SpriteContentsAccess) contents).radiante$mipImages();
        if (mips.length == 0 || mips[0] == null) {
            return;
        }
        ByteBuffer pixels = mips[0];
        int width = contents.width();
        int height = contents.height();
        int rowPixels = Math.max(1, ((SpriteContentsAccess) contents).radiante$mipWidth(0));
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        int cellWidth = Math.max(1, width / CELLS_PER_SIDE);
        int cellHeight = Math.max(1, height / CELLS_PER_SIDE);
        float boost = lightBoost(sprite);

        List<float[]> cells = new ArrayList<>();
        for (int cellY = 0; cellY < height; cellY += cellHeight) {
            for (int cellX = 0; cellX < width; cellX += cellWidth) {
                float emission = 0.0f;
                float red = 0.0f;
                float green = 0.0f;
                float blue = 0.0f;
                int texels = 0;
                for (int y = cellY; y < Math.min(height, cellY + cellHeight); y++) {
                    for (int x = cellX; x < Math.min(width, cellX + cellWidth); x++) {
                        int index = (y * rowPixels + x) * 4;
                        int merIndex = ((originY + y) * atlasWidth + originX + x) * 4;
                        if (index + 3 >= pixels.capacity() || merIndex + 3 >= mer.capacity()) {
                            continue;
                        }
                        texels++;
                        // A cut out texel (alpha under a half) is not drawn, and gives no light.
                        if ((pixels.get(index + 3) & 0xFF) < 128) {
                            continue;
                        }
                        float g = (mer.get(merIndex + 1) & 0xFF) / 255.0f;
                        float weight = g * g;
                        if (weight <= 0.0f) {
                            continue;
                        }
                        emission += weight;
                        red += srgbToLinear(pixels.get(index) & 0xFF) * weight;
                        green += srgbToLinear(pixels.get(index + 1) & 0xFF) * weight;
                        blue += srgbToLinear(pixels.get(index + 2) & 0xFF) * weight;
                    }
                }
                if (emission <= 0.0f || texels == 0) {
                    continue;
                }
                float u0 = (originX + cellX) / (float) atlasWidth;
                float v0 = (originY + cellY) / (float) atlasHeight;
                float u1 = (originX + Math.min(width, cellX + cellWidth)) / (float) atlasWidth;
                float v1 = (originY + Math.min(height, cellY + cellHeight)) / (float) atlasHeight;
                // Lava lights its surroundings red-orange, with the cast of a sea of it held down (see LAVA_MER_CAST).
                boolean lava = isLava(sprite);
                cells.add(new float[] {u0, v0, u1, v1, -emission / texels * boost * (lava ? LAVA_MER_CAST : 1.0f),
                    red / emission, green / emission * (lava ? LAVA_LIGHT_GREEN : 1.0f),
                    blue / emission * (lava ? LAVA_LIGHT_BLUE : 1.0f)});
            }
        }
        long tileKey = ((long) sprite.getX() << 32) ^ sprite.getY() ^ ((long) width << 48) ^ height;
        if (cells.isEmpty()) {
            TextureProxy.uploadEmissionTile(atlasId, tileKey, 0L, 0);
            return;
        }
        long address = MemoryUtil.nmemAllocChecked((long) cells.size() * CELL_BYTES);
        try {
            for (int i = 0; i < cells.size(); i++) {
                float[] cell = cells.get(i);
                for (int j = 0; j < cell.length; j++) {
                    MemoryUtil.memPutFloat(address + (long) i * CELL_BYTES + (long) j * Float.BYTES, cell[j]);
                }
            }
            TextureProxy.uploadEmissionTile(atlasId, tileKey, address, cells.size());
        } finally {
            MemoryUtil.nmemFree(address);
        }
    }

    /** The share of a firefly bush's pack glow that is kept: its few specks are bright, and were glaring. */
    private static final float FIREFLY_GLOW = 0.25f;

    /**
     * Tones down a block whose pack glow is a few very bright specks (the firefly bush): emission is the MER green
     * squared, so its green is scaled by the root of the share kept. Done on the map itself, before the light is
     * taken from it, so the specks and the light they cast are softened together.
     */
    static void softenMer(TextureAtlasSprite sprite, ByteBuffer mer, int atlasWidth, int atlasHeight) {
        boolean lava = isLava(sprite);
        if (!lava && !sprite.contents().name().getPath().startsWith("block/firefly_bush")) {
            return;
        }
        float scale = (float) Math.sqrt(lava ? LAVA_MER_GLOW : FIREFLY_GLOW);
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        for (int y = 0; y < sprite.contents().height(); y++) {
            for (int x = 0; x < sprite.contents().width(); x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 1;
                if (index < mer.capacity()) {
                    mer.put(index, (byte) Math.round((mer.get(index) & 0xFF) * scale));
                }
            }
        }
    }

    /**
     * The same softening for a block whose glow comes from a LabPBR specular map (alpha 1..254, linear) rather than a
     * MER one: the vanilla textures' own specks, and a pack's _s maps.
     */
    static void softenSpecular(TextureAtlasSprite sprite, ByteBuffer specular, int atlasWidth, int atlasHeight) {
        if (!sprite.contents().name().getPath().startsWith("block/firefly_bush")) {
            return;
        }
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        for (int y = 0; y < sprite.contents().height(); y++) {
            for (int x = 0; x < sprite.contents().width(); x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                if (index < specular.capacity()) {
                    int value = specular.get(index) & 0xFF;
                    if (value != 0 && value != 255) {
                        specular.put(index, (byte) Math.max(1, Math.round(value * FIREFLY_GLOW)));
                    }
                }
            }
        }
    }

    /** The exact sRGB decoding of an 8-bit value, as Bedrock RTX reads its colour textures. */
    private static float srgbToLinear(int value) {
        float c = value / 255.0f;
        return c <= 0.04045f ? c / 12.92f : (float) Math.pow((c + 0.055f) / 1.055f, 2.4f);
    }

    private static boolean hasWhiteCore(TextureAtlasSprite sprite) {
        return sprite.contents().name().getPath().endsWith("copper_lantern");
    }

    /**
     * How much more light than its pack gives it a block casts (the Bedrock profile's Enhanced extras). Glow berries
     * are a plant that a pack lights only on its berries, a few specks, so a lush cave of them was nearly dark; they
     * now light it as the warm lanterns the level 14 of vanilla's berries suggests.
     */
    private static float lightBoost(TextureAtlasSprite sprite) {
        String path = sprite.contents().name().getPath();
        return path.equals("block/cave_vines_lit") || path.equals("block/cave_vines_plant_lit") ? GLOW_BERRY_LIGHT_BOOST : 1.0f;
    }

    private static final float GLOW_BERRY_LIGHT_BOOST = 5.0f;

    private static boolean glowsAllOver(TextureAtlasSprite sprite) {
        String path = sprite.contents().name().getPath();
        // The glow berry vines too: only the berries glowed, a few specks of a dark plant, so a cave of them was barely
        // lit. Each texel by its brightness, the leaves dim and the berries bright, they light their surroundings softly.
        return path.equals("block/beacon") || path.equals(END_PORTAL_EYE.getPath()) || path.equals("block/cave_vines_lit")
            || path.equals("block/cave_vines_plant_lit");
    }

    /** How brightly lava's surface glows against a level 15 light's: at full it was overexposed into a pale peach. */
    private static final float LAVA_SURFACE_GLOW = 0.09f;

    /** How much of a level 15 light's cast each lava block gives the blocks around it. */
    private static final float LAVA_CAST = 0.12f;

    /** The share of the green and blue of lava's texels its light keeps: an orange-red light, not a white one. */
    private static final float LAVA_LIGHT_GREEN = 0.5f;
    private static final float LAVA_LIGHT_BLUE = 0.12f;

    /**
     * Under the Bedrock profile lava glows and casts what its pack's MER map says (uploadFromMer), so none of the
     * legacy tuning above (surface glow, cast, light colour) reached it: a sea of lava, glowing at the pack's full
     * strength, burnt the Nether out to pale peach and a flat red. The share of that glow kept (the MER green is
     * scaled by its root), and the share of it a lava block casts on the blocks around it on top.
     */
    private static final float LAVA_MER_GLOW = 0.4f;
    private static final float LAVA_MER_CAST = 0.3f;

    private static boolean isLava(TextureAtlasSprite sprite) {
        Identifier name = sprite.contents().name();
        for (Identifier id : LAVA_SPRITES) {
            if (id.equals(name)) {
                return true;
            }
        }
        return false;
    }

    private static float maxBrightness(ByteBuffer pixels, int width, int height, int rowPixels) {
        float max = 0.0f;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = (y * rowPixels + x) * 4;
                if (index + 3 >= pixels.capacity() || (pixels.get(index + 3) & 0xFF) < 26) {
                    continue;
                }
                max = Math.max(max, Math.max((pixels.get(index) & 0xFF),
                    Math.max(pixels.get(index + 1) & 0xFF, pixels.get(index + 2) & 0xFF)) / 255.0f);
            }
        }
        return max;
    }

    private static boolean hasSaturatedTexels(ByteBuffer pixels, int width, int height, int rowPixels,
        float threshold) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = (y * rowPixels + x) * 4;
                if (index + 3 >= pixels.capacity() || (pixels.get(index + 3) & 0xFF) < 26) {
                    continue;
                }
                float r = (pixels.get(index) & 0xFF) / 255.0f;
                float g = (pixels.get(index + 1) & 0xFF) / 255.0f;
                float b = (pixels.get(index + 2) & 0xFF) / 255.0f;
                if (Math.max(r, Math.max(g, b)) >= threshold && saturation(r, g, b) >= MIN_GLOW_SATURATION) {
                    return true;
                }
            }
        }
        return false;
    }

    private static float saturation(float r, float g, float b) {
        float max = Math.max(r, Math.max(g, b));
        float min = Math.min(r, Math.min(g, b));
        return max <= 0.0f ? 0.0f : (max - min) / max;
    }

    /** The specular texture mapped onto the block atlas, used by BufferProxy.updateMapping. */
    public static int specularTextureFor(int albedoTextureId) {
        return albedoTextureId == specularAtlasId ? specularTextureId : -1;
    }

    /** A sprite whose glow Radiante sets itself, whatever a pack gives it: see {@link #keepSetGlow}. */
    static boolean setsOwnGlow(Identifier spriteId) {
        // Not under the Bedrock profile: there the eye glows and casts what its pack's MER map says, as in Bedrock
        // RTX (Vanilla RTX: 5 of 255, next to nothing). Kept out of the MER, its worked out level-8 glow lit a
        // closed room brighter than three torches.
        return END_PORTAL_EYE.equals(spriteId) && !com.g2806.radiante.client.pipeline.Pipeline.castsBedrockGlow();
    }

    /** LabPBR stores emission in the specular alpha: 0..254 scale linearly, 255 means none. */
    /**
     * A sprite whose glow Radiante sets itself (an eye of ender in its frame) keeps that strength under a pack's
     * own specular map: the pack's emission gives the shape, and its brightest texel is brought up to the level set
     * here. Packs author emission on scales of their own - Vanilla RTX gives the eye 5 of 255, a glow too faint to
     * see - and the light the eye casts already follows the level, so the eye lit the frame around it without
     * visibly glowing itself. A map with no emission at all leaves the whole sprite glowing.
     */
    static void keepSetGlow(TextureAtlasSprite sprite, ByteBuffer specular, int atlasWidth, int atlasHeight) {
        if (!setsOwnGlow(sprite.contents().name())) {
            return;
        }
        SpriteContents contents = sprite.contents();
        int width = contents.width();
        int height = contents.height();
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        float strength = END_PORTAL_EYE_GLOW;
        // LabPBR's emission: 1 to 254 from faintest to full, 0 and 255 none.
        int peak = 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                if (index < specular.capacity()) {
                    int value = specular.get(index) & 0xFF;
                    peak = Math.max(peak, value == 255 ? 0 : value);
                }
            }
        }
        ByteBuffer[] mips = ((SpriteContentsAccess) contents).radiante$mipImages();
        ByteBuffer pixels = mips.length == 0 ? null : mips[0];
        int rowPixels = Math.max(1, ((SpriteContentsAccess) contents).radiante$mipWidth(0));
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                if (index >= specular.capacity()) {
                    continue;
                }
                int value = specular.get(index) & 0xFF;
                if (peak > 0) {
                    if (value > 0 && value < 255 && peak < strength * 254.0f) {
                        writeSpecularEmission(specular, atlasWidth, atlasHeight, originX + x, originY + y,
                            strength * value / peak);
                    }
                    continue;
                }
                int pixel = (y * rowPixels + x) * 4;
                if (pixels == null || pixel + 3 >= pixels.capacity() || (pixels.get(pixel + 3) & 0xFF) < 26) {
                    continue;
                }
                writeSpecularEmission(specular, atlasWidth, atlasHeight, originX + x, originY + y, strength);
            }
        }
    }

    /**
     * The glow Radiante worked out for a light block's sprite (see {@link #upload}), kept before a pack's specular map
     * is laid over it, for {@link #keepVisibleGlow}; null for a sprite that gives no light.
     */
    static byte[] derivedGlow(TextureAtlasSprite sprite, ByteBuffer specular, int atlasWidth, int atlasHeight) {
        Integer level = lightLevels.get(sprite);
        if (level == null || level <= 0) {
            return null;
        }
        int width = sprite.contents().width();
        int height = sprite.contents().height();
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        byte[] glow = new byte[width * height];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                if (index < specular.capacity()) {
                    glow[y * width + x] = specular.get(index);
                }
            }
        }
        return glow;
    }

    /**
     * A light block whose pack gives it little glow - Vanilla RTX authors its redstone ore with an emission of 4 of
     * 255, its redstone torch 13, a block of redstone 8 - lit everything around it without seeming to give that light
     * itself: what it casts follows its light level, what it shows followed the pack alone. The pack's map keeps the
     * shape (which texels glow, and how much more than each other), and its brightest texel is brought up to what a
     * light of that level shows when Radiante works the glow out itself (the level times that texel's brightness); a
     * map that gives the block no glow at all gives way to the worked out one. Lights the pack already makes brighter
     * than that are left as they are. (The experimental Bedrock profile shows and casts the glow of the MER map as
     * the pack authors it, as Bedrock RTX does: see uploadFromMer.)
     */
    static void keepVisibleGlow(TextureAtlasSprite sprite, ByteBuffer specular, byte[] derived, int atlasWidth,
        int atlasHeight) {
        Integer level = lightLevels.get(sprite);
        // The eye of ender in its frame is no lamp: it shows the glow its pack gives it (see setsOwnGlow).
        if (level == null || level <= 0 || END_PORTAL_EYE.equals(sprite.contents().name())) {
            return;
        }
        SpriteContents contents = sprite.contents();
        int width = contents.width();
        int height = contents.height();
        int originX = Math.round(sprite.getU0() * atlasWidth);
        int originY = Math.round(sprite.getV0() * atlasHeight);
        ByteBuffer[] mips = ((SpriteContentsAccess) contents).radiante$mipImages();
        ByteBuffer pixels = mips.length == 0 ? null : mips[0];
        int rowPixels = Math.max(1, ((SpriteContentsAccess) contents).radiante$mipWidth(0));

        // LabPBR's emission: 1 to 254 from faintest to full, 0 and 255 none.
        int peak = 0;
        float brightest = 0.0f;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                int value = index < specular.capacity() ? specular.get(index) & 0xFF : 0;
                if (value == 0 || value == 255) {
                    continue;
                }
                peak = Math.max(peak, value);
                int pixel = (y * rowPixels + x) * 4;
                if (pixels != null && pixel + 2 < pixels.capacity()) {
                    brightest = Math.max(brightest, Math.max(pixels.get(pixel) & 0xFF,
                        Math.max(pixels.get(pixel + 1) & 0xFF, pixels.get(pixel + 2) & 0xFF)) / 255.0f);
                }
            }
        }
        if (peak == 0) {
            if (derived == null) {
                return;
            }
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                    if (index < specular.capacity()) {
                        specular.put(index, derived[y * width + x]);
                    }
                }
            }
        } else {
            int target = Math.round(254.0f * level / 15.0f * (pixels == null ? 1.0f : brightest));
            if (peak < target) {
                float scale = target / (float) peak;
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int index = ((originY + y) * atlasWidth + originX + x) * 4 + 3;
                        int value = index < specular.capacity() ? specular.get(index) & 0xFF : 0;
                        if (value != 0 && value != 255) {
                            specular.put(index, (byte) Math.max(1, Math.min(254, Math.round(value * scale))));
                        }
                    }
                }
            }
        }

    }

    private static void writeSpecularEmission(ByteBuffer specular, int width, int height, int x, int y,
        float emission) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        int value = Math.max(1, Math.min(254, Math.round(emission * 254.0f)));
        specular.put((y * width + x) * 4 + 3, (byte) value);
    }

    /**
     * Everything outside emissive texels stays zero, which the shaders read exactly like a missing specular
     * texture, so only the emission changes.
     */
    private static void uploadSpecularAtlas(int atlasId, ByteBuffer level0, int width, int height, int mipLevels) {
        if (specularTextureId < 0) {
            specularTextureId = TextureProxy.generateTextureId();
            specularWidth = 0;
        }
        if (specularWidth != width || specularHeight != height) {
            TextureProxy.prepareImage(specularTextureId, mipLevels, width, height,
                com.mojang.renderpearl.backend.vulkan.VulkanConst.toVk(com.mojang.renderpearl.api.GpuFormat.RGBA8_UNORM));
            specularWidth = width;
            specularHeight = height;
        }

        ByteBuffer current = level0;
        int levelWidth = width;
        int levelHeight = height;
        for (int level = 0; level < mipLevels; level++) {
            TextureProxy.queueUpload(MemoryUtil.memAddress(current), levelWidth * levelHeight * 4, levelWidth,
                specularTextureId, 0, 0, 0, 0, levelWidth, levelHeight, level);
            if (level + 1 >= mipLevels) {
                break;
            }

            int nextWidth = Math.max(1, levelWidth / 2);
            int nextHeight = Math.max(1, levelHeight / 2);
            ByteBuffer next = MemoryUtil.memCalloc(nextWidth * nextHeight * 4);
            for (int y = 0; y < nextHeight; y++) {
                for (int x = 0; x < nextWidth; x++) {
                    int max = 0;
                    for (int dy = 0; dy < 2; dy++) {
                        for (int dx = 0; dx < 2; dx++) {
                            int sx = Math.min(levelWidth - 1, x * 2 + dx);
                            int sy = Math.min(levelHeight - 1, y * 2 + dy);
                            max = Math.max(max, current.get((sy * levelWidth + sx) * 4 + 3) & 0xFF);
                        }
                    }
                    next.put((y * nextWidth + x) * 4 + 3, (byte) max);
                }
            }
            if (current != level0) {
                MemoryUtil.memFree(current);
            }
            current = next;
            levelWidth = nextWidth;
            levelHeight = nextHeight;
        }
        if (current != level0) {
            MemoryUtil.memFree(current);
        }
        specularAtlasId = atlasId;
    }
}
