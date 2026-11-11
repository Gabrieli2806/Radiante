package com.g2806.radiante.compat;

import com.g2806.radiante.client.gui.RadianteDebugEntries;
import com.mojang.blaze3d.Blaze3D;
import com.mojang.blaze3d.platform.InputConstants;
import com.mojang.blaze3d.platform.NativeImage;
import com.mojang.renderpearl.api.GpuFormat;
import com.mojang.renderpearl.api.textures.GpuTexture;
import com.mojang.renderpearl.api.textures.GpuTextureView;
import com.mojang.renderpearl.backend.vulkan.VulkanConst;
import java.nio.file.Path;
import java.util.function.Supplier;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.renderer.RenderPipelines;
import net.minecraft.client.renderer.state.level.SkyRenderState;
import net.minecraft.client.renderer.texture.AbstractTexture;
import net.minecraft.client.renderer.texture.DynamicTexture;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.resources.model.geometry.BakedQuad;
import net.minecraft.core.Direction;
import net.minecraft.core.Holder;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.attribute.EnvironmentAttributes;
import net.minecraft.world.level.CardinalLighting;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.biome.Biomes;
import net.minecraft.world.level.block.RedstoneWireBlock;
import net.minecraft.world.level.block.entity.SignBlockEntity;
import net.minecraft.world.level.block.entity.SignText;
import net.minecraft.world.level.block.entity.SignTextSlot;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.ChunkGenerator;
import net.minecraft.world.level.levelgen.Heightmap;
import net.minecraft.world.level.levelgen.RandomState;
import org.joml.Vector3fc;
import org.jspecify.annotations.Nullable;

/**
 * Where Minecraft's API differs between the versions Radiante is built for, in one place. Each version's branch
 * (mc/26.2, mc/26.3, mc/26.4) has this class with the same members and bodies of its own, so the classes that call
 * it are the same file on every branch and a change to them merges across without a conflict.
 * <p>
 * A call that differs between versions in an ordinary class goes here, as a small static method named for what it
 * does, added to every branch with the same signature. Mixins and accessors stay where they are: their targets are
 * a version's own. This is not {@code client.compat}, which is for other mods.
 * <p>
 * This is the one for Minecraft 26.3.
 */
public final class VersionCompat {

    /** The resource pack format of this Minecraft version: 88 on 26.2, 97 on 26.3, 100 on 26.4. */
    public static final int RESOURCE_PACK_FORMAT = 97;

    private VersionCompat() {
    }

    // ---- window and input ----

    /** Lets the window answer the system while the render thread is busy: GLFW's poll on 26.2, SDL's pump since. */
    public static void pumpWindowEvents() {
        org.lwjgl.sdl.SDLEvents.SDL_PumpEvents();
    }

    /** Whether a key is held: 26.2 asks it of the window, later versions of the keyboard as a whole. */
    public static boolean isKeyDown(int key) {
        return InputConstants.isKeyDown(key);
    }

    /**
     * A key press as a screen receives it: 26.2 carries GLFW's scancode for the key, asked of GLFW; later versions
     * carry {@code sdlCode}, and 26.4 also whether the press is a repeat.
     */
    public static KeyEvent keyEvent(int key, int sdlCode, int modifiers) {
        return new KeyEvent(key, sdlCode, modifiers);
    }

    /** Opens a folder in the system's file manager: Util's platform on 26.2, Blaze3D since. */
    public static void openFolder(Path folder) {
        Blaze3D.openPath(folder);
    }

    // ---- screens ----

    /** A textured quad of the GUI, through RenderPipelines.GUI_TEXTURED up to 26.3 and GuiPipeline.TEXTURED on 26.4. */
    public static void blit(GuiGraphicsExtractor graphics, Identifier texture, int x, int y, float u, float v,
        int width, int height, int textureWidth, int textureHeight) {
        graphics.blit(RenderPipelines.GUI_TEXTURED, texture, x, y, u, v, width, height, textureWidth, textureHeight);
    }

    /** As the shorter blit, of a region of the texture scaled to the quad; the same pipeline difference. */
    public static void blit(GuiGraphicsExtractor graphics, Identifier texture, int x, int y, float u, float v,
        int width, int height, int regionWidth, int regionHeight, int textureWidth, int textureHeight) {
        graphics.blit(RenderPipelines.GUI_TEXTURED, texture, x, y, u, v, width, height, regionWidth, regionHeight,
            textureWidth, textureHeight);
    }

    /** A sprite of the GUI atlas; the same pipeline difference as blit. */
    public static void blitSprite(GuiGraphicsExtractor graphics, Identifier sprite, int x, int y, int width,
        int height) {
        graphics.blitSprite(RenderPipelines.GUI_TEXTURED, sprite, x, y, width, height);
    }

    /**
     * A screen whose drawing has one shape on every version: up to 26.3 Minecraft hands a screen the mouse and the
     * partial tick, on 26.4 only the graphics, which know where the mouse is.
     */
    public abstract static class DrawnScreen extends Screen {

        protected DrawnScreen(Component title) {
            super(title);
        }

        /** What the screen draws over its widgets, each frame. */
        protected void extractContents(GuiGraphicsExtractor graphics, int mouseX, int mouseY) {
        }

        /** The screen's own backdrop, returning true; false leaves Minecraft's. */
        protected boolean extractOwnBackground(GuiGraphicsExtractor graphics) {
            return false;
        }

        @Override
        public void extractRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
            super.extractRenderState(graphics, mouseX, mouseY, partialTick);
            extractContents(graphics, mouseX, mouseY);
        }

        @Override
        public void extractBackground(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
            if (!extractOwnBackground(graphics)) {
                super.extractBackground(graphics, mouseX, mouseY, partialTick);
            }
        }
    }

    /**
     * Radiante's lines of the debug screen: registered by the mod up to 26.3; on 26.4 they are a registry Minecraft
     * fills at bootstrap (DebugScreenEntriesMixin) and there is nothing left to do here.
     */
    public static void registerDebugEntries() {
        RadianteDebugEntries.register();
    }

    // ---- textures ----

    /**
     * Registers a texture made of an image. True when the texture owns the image from here on and closes it with
     * itself (a DynamicTexture, up to 26.3); false when the pixels were copied and the image is still the caller's to
     * close (26.4's TextureResources).
     */
    public static boolean registerImageTexture(Identifier id, Supplier<String> label, NativeImage image) {
        DynamicTexture texture = new DynamicTexture(label, image);
        Minecraft.getInstance().getTextureManager().register(id, texture);
        return true;
    }

    /** An atlas's GPU image, which has to exist: the atlas hands it out itself up to 26.3, through a view on 26.4. */
    public static GpuTexture gpuTexture(TextureAtlas atlas) {
        return atlas.getTexture();
    }

    /** An atlas's GPU image, or null before it has one; asking throws then, on every version. */
    public static @Nullable GpuTexture gpuTextureOrNull(TextureAtlas atlas) {
        return gpuTextureOrNull((AbstractTexture) atlas);
    }

    /**
     * The GPU image of the texture registered under a name, or null without one: the texture manager holds
     * AbstractTextures up to 26.3 and TextureHandles, with a view in between, on 26.4.
     */
    public static @Nullable GpuTexture gpuTextureOrNull(Identifier location) {
        AbstractTexture texture = Minecraft.getInstance().getTextureManager().getTexture(location);
        return texture == null ? null : gpuTextureOrNull(texture);
    }

    private static @Nullable GpuTexture gpuTextureOrNull(AbstractTexture texture) {
        try {
            return texture.getTexture();
        } catch (IllegalStateException notCreatedYet) {
            return null;
        }
    }

    /** Whether the texture registered under a name has one channel, as a font page without colour; as above. */
    public static boolean isSingleChannelTexture(Identifier location) {
        AbstractTexture texture = Minecraft.getInstance().getTextureManager().getTexture(location);
        return texture != null && texture.getTexture() != null
            && texture.getTexture().getFormat() == GpuFormat.R8_UNORM;
    }

    /**
     * The texture behind a texture view another mod handed over as an Object, or null for null: the view's class is
     * blaze3d's on 26.2 and renderpearl's since.
     */
    public static @Nullable GpuTexture textureOfView(@Nullable Object textureView) {
        return textureView == null ? null : ((GpuTextureView) textureView).texture();
    }

    /** Vulkan's number for the RGBA8 format: GpuFormat and VulkanConst are blaze3d's on 26.2, renderpearl's since. */
    public static int vkFormatRgba8() {
        return VulkanConst.toVk(GpuFormat.RGBA8_UNORM);
    }

    // ---- world ----

    /** Whether the sky has its dark disc under the horizon: shouldRenderDarkDisc up to 26.3, hasSkyOccluder on 26.4. */
    public static boolean drawsDarkDisc(SkyRenderState sky) {
        return sky.shouldRenderDarkDisc;
    }

    /** A sign's writing on its front: picked by a boolean on 26.2, by a SignTextSlot since. */
    public static SignText signFrontText(SignBlockEntity sign) {
        return sign.getText(SignTextSlot.FRONT);
    }

    /** A sign's writing on its back; as signFrontText. */
    public static SignText signBackText(SignBlockEntity sign) {
        return sign.getText(SignTextSlot.BACK);
    }

    /**
     * Vanilla's fixed shade of a quad's face: 26.2 shades a quad by its own face or, unshaded, as one facing up;
     * later versions let the quad name another face to be shaded as.
     */
    public static float faceShade(CardinalLighting cardinal, BakedQuad quad) {
        Direction shadeFace = quad.materialInfo().shadeDirectionOverride();
        return cardinal.byFace(shadeFace != null ? shadeFace : quad.direction());
    }

    /** The power of redstone dust: the block's class is RedStoneWireBlock on 26.2, RedstoneWireBlock since. */
    public static int redstoneWirePower(BlockState dust) {
        return dust.getValue(RedstoneWireBlock.POWER);
    }

    /** The dappled forest, for a table of biomes: Minecraft has it since 26.3, and 26.2 gets an empty list. */
    @SuppressWarnings("unchecked")
    public static ResourceKey<Biome>[] dappledForest() {
        return new ResourceKey[] {Biomes.DAPPLED_FOREST};
    }

    /**
     * A biome's fog colour, or {@code unchanged} (the overworld's) where the biome leaves it alone: a packed RGB
     * on 26.2, a vector since.
     */
    public static Vector3fc biomeFogColor(Biome biome, Vector3fc unchanged) {
        return biome.getAttributes().applyModifier(EnvironmentAttributes.FOG_COLOR, unchanged);
    }

    /** The biome the generator puts at a block, no chunk loaded: asked by block on 26.4, by 4-block cell before. */
    public static Holder<Biome> biomeAt(ServerLevel level, int x, int y, int z) {
        return level.getUncachedNoiseBiome(x >> 2, y >> 2, z >> 2);
    }

    /** The height of the generator's terrain in a column: getBaseHeight up to 26.3, getFirstFreeHeight on 26.4. */
    public static int surfaceHeight(ChunkGenerator generator, int x, int z, Heightmap.Types heightmap,
        ServerLevel level, RandomState random) {
        return generator.getBaseHeight(x, z, heightmap, level, random);
    }
}
