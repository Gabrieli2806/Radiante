package com.g2806.radiante.client.render;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.proxy.world.ChunkProxy;
import com.g2806.radiante.compat.VersionCompat;
import it.unimi.dsi.fastutil.longs.Long2ByteOpenHashMap;
import it.unimi.dsi.fastutil.longs.LongArrayList;
import it.unimi.dsi.fastutil.longs.LongIterator;
import it.unimi.dsi.fastutil.longs.LongOpenHashSet;
import java.util.ArrayList;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import net.minecraft.client.Minecraft;
import net.minecraft.client.SectionUpdateTracker;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.renderer.block.BlockQuadOutput;
import net.minecraft.client.renderer.block.FluidRenderer;
import net.minecraft.client.renderer.block.ModelBlockRenderer;
import net.minecraft.client.renderer.chunk.ChunkSectionLayer;
import net.minecraft.client.renderer.chunk.RenderRegionCache;
import net.minecraft.client.renderer.chunk.RenderSectionRegion;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.resources.model.geometry.BakedQuad;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.SectionPos;
import net.minecraft.tags.BlockTags;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.RenderShape;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FluidState;
import org.joml.Vector3fc;
import org.lwjgl.system.MemoryUtil;

import static com.g2806.radiante.client.render.ChunkManager.*;

/**
 * What kind of block a block is to the section builder ({@link ChunkManager}): glass, a blended block, a plant,
 * leaves, frost, a thin block, and how it sways in the wind. Asked once per block while a section is meshed; the
 * answers go into the vertices the shaders read.
 */
final class BlockKinds {

    private BlockKinds() {
    }

    /**
     * Glass blocks (plain, stained, tinted) and glass panes. Their see-through texels are clear glass for the tracer,
     * as Bedrock RTX renders them: a surface that reflects and refracts, not a hole. Iron bars share the pane class.
     */
    static boolean isGlass(BlockState state) {
        net.minecraft.world.level.block.Block block = state.getBlock();
        // Tinted glass too: seen through like the rest, only darkened by its own deep tint.
        // Not copper grates, which share glass's class: their holes are holes, and as clear glass they mirrored the
        // sky and bent the view through them.
        return block instanceof net.minecraft.world.level.block.TransparentBlock
                && !(block instanceof net.minecraft.world.level.block.WaterloggedTransparentBlock)
            || state.is(Blocks.TINTED_GLASS)
            || block instanceof net.minecraft.world.level.block.StainedGlassPaneBlock
            || state.is(net.minecraft.world.level.block.Blocks.GLASS_PANE);
    }



    /**
     * The other blocks Bedrock draws blended, as it does glass: ice, frosted ice, slime and honey. Bedrock RTX sees
     * through them the way it sees through glass; radiante-pt does the same.
     */
    static boolean isBlend(BlockState state) {
        net.minecraft.world.level.block.Block block = state.getBlock();
        return block instanceof net.minecraft.world.level.block.HalfTransparentBlock
            && !(block instanceof net.minecraft.world.level.block.TransparentBlock);
    }

    /** Glass panes (plain and stained), as opposed to glass blocks: seamless glass can treat them apart. */
    static boolean isGlassPane(BlockState state) {
        return state.getBlock() instanceof net.minecraft.world.level.block.StainedGlassPaneBlock
            || state.is(net.minecraft.world.level.block.Blocks.GLASS_PANE);
    }

    /** Grass, ferns, flowers, bushes and the like: what the tracer may leave out far away (foliage culling). */
    static boolean isPlant(BlockState state) {
        return state.getBlock() instanceof net.minecraft.world.level.block.VegetationBlock;
    }

    /** Blocks of a column a stalk of sugar cane or kelp counts below itself for how far its top may sway. */
    static final int WIND_COLUMN_LIMIT = 6;

    /**
     * How a block sways in the wind (PBRVertexWriter.WIND_*), or WIND_NONE. Leaves and what hangs (vines, roots, moss)
     * rustle in place; grass, flowers, crops, saplings and sugar cane sway about their foot; seagrass and kelp sway
     * slowly with the current. Mushrooms stay still: they are stiff caps, not stalks.
     */
    static int windKindOf(BlockState state) {
        net.minecraft.world.level.block.Block block = state.getBlock();
        if (state.is(BlockTags.LEAVES) || block instanceof net.minecraft.world.level.block.VineBlock
            || block == Blocks.CAVE_VINES || block == Blocks.CAVE_VINES_PLANT || block == Blocks.WEEPING_VINES
            || block == Blocks.WEEPING_VINES_PLANT || block == Blocks.HANGING_ROOTS
            || block == Blocks.PALE_HANGING_MOSS || block == Blocks.LILY_PAD) {
            return PBRVertexWriter.WIND_LEAVES;
        }
        if (block == Blocks.SEAGRASS || block == Blocks.TALL_SEAGRASS || block == Blocks.KELP
            || block == Blocks.KELP_PLANT) {
            return PBRVertexWriter.WIND_WATER;
        }
        if (block == Blocks.SUGAR_CANE || block instanceof net.minecraft.world.level.block.VegetationBlock
            && !(block instanceof net.minecraft.world.level.block.MushroomBlock)) {
            return PBRVertexWriter.WIND_GROUND;
        }
        return PBRVertexWriter.WIND_NONE;
    }

    /**
     * Sets the scratch's wind for one block: its kind, its column, and the rise and scale that turn a vertex's height
     * in the block into its weight (PBRVertexWriter.wind). The top half of a tall plant starts where the bottom half
     * ends; a stalk of sugar cane or kelp counts the blocks of it below, so its blocks bend as one and never come apart.
     */
    static void setWind(SectionCompileScratch scratch, RenderSectionRegion region, BlockPos pos,
        BlockState state, int sy) {
        int kind = windKindOf(state);
        scratch.windKind = kind;
        scratch.windBaseY = sy;
        scratch.windRise = 0.0f;
        scratch.windScale = 1.0f;
        if (kind == PBRVertexWriter.WIND_NONE) {
            return;
        }
        // The low three bits of x and z are enough: with a vertex's position they single out its block.
        scratch.windColumn = (pos.getX() & 7) | (pos.getZ() & 7) << 3;
        net.minecraft.world.level.block.Block block = state.getBlock();
        if (kind == PBRVertexWriter.WIND_LEAVES) {
            // Leaves move most; what hangs a little less, a lily pad least, as it floats.
            scratch.windScale = state.is(BlockTags.LEAVES) ? 1.0f : block == Blocks.LILY_PAD ? 0.4f : 0.7f;
            return;
        }
        if (state.hasProperty(net.minecraft.world.level.block.state.properties.BlockStateProperties.DOUBLE_BLOCK_HALF)
            && state.getValue(net.minecraft.world.level.block.state.properties.BlockStateProperties.DOUBLE_BLOCK_HALF)
                == net.minecraft.world.level.block.state.properties.DoubleBlockHalf.UPPER) {
            scratch.windRise = 1.0f;
        }
        boolean column = block == Blocks.SUGAR_CANE || block == Blocks.KELP || block == Blocks.KELP_PLANT;
        if (column) {
            int below = 0;
            BlockPos.MutableBlockPos cursor = pos.mutable();
            while (below < WIND_COLUMN_LIMIT) {
                net.minecraft.world.level.block.Block under =
                    region.getBlockState(cursor.move(Direction.DOWN)).getBlock();
                boolean same = block == Blocks.SUGAR_CANE ? under == Blocks.SUGAR_CANE
                    : under == Blocks.KELP || under == Blocks.KELP_PLANT;
                if (!same) {
                    break;
                }
                below++;
            }
            // Stiffer than grass: a stalk of three sways at its top as far as a tall flower.
            scratch.windScale = 0.5f;
            scratch.windRise = below * 0.5f;
        }
    }

    /**
     * Whether a quad of a lit redstone torch is one of its halo: the six faces of a box three pixels wide around the
     * torch's head, turned inward, each showing one red texel. Minecraft draws them one sided, so from any side the
     * far ones show as a dark red rim behind the head. Traced, a face is seen from both sides and these glow like the
     * head, so the torch's head was a flat, pale red cube half again its size with none of its texture. They are
     * written as a see-through shell instead (TORCH_HALO_ALPHA of the rays meet it, the rest pass), with a faint red
     * glow of its own: the head shows through it with its texture, inside a soft red box. Told by size: the torch's
     * own faces are two pixels wide.
     */
    static final float TORCH_HALO_ALPHA = 0.3f;
    static final float TORCH_HALO_GLOW = Glow.ofLevel(4);

    /**
     * The texel of the torch's texture a wall torch's halo is drawn with, in sixteenths: the middle of the red ring's
     * top edge. Vanilla's wall torch model paints every face of its halo with the ring's corner texel (6, 5), which a
     * Bedrock texture leaves clear (Vanilla RTX's ring has no corners): there the wall torch had no halo at all while
     * the standing one, painted with the edges, had it.
     */
    static final float WALL_TORCH_HALO_U = 7.5f / 16.0f;
    static final float WALL_TORCH_HALO_V = 5.5f / 16.0f;

    static boolean isTorchHalo(net.minecraft.client.resources.model.geometry.BakedQuad quad) {
        float limit = 2.5f / 16.0f;
        return quad.position0().distance(quad.position1()) > limit
            && quad.position1().distance(quad.position2()) > limit;
    }

    /** Leaves of every tree (the LEAVES tag): foliage a shader pack may treat apart from small plants. */
    static boolean isLeaves(BlockState state) {
        return state.is(BlockTags.LEAVES);
    }

    /** Ice of every kind and snow, which the sun lights a little past the edge of its light (PBRVertexWriter.frost). */
    static boolean isFrost(BlockState state) {
        return state.is(BlockTags.ICE) || state.is(BlockTags.SNOW);
    }

    /**
     * Thin foliage and fibres that let light through, besides leaves and small plants (isLeaves, isPlant): what hangs
     * (vines, roots, moss), stalks and floating leaves, and cobwebs. Not marked as plants, which the tracer leaves out
     * far away.
     */
    static boolean isThin(BlockState state) {
        net.minecraft.world.level.block.Block block = state.getBlock();
        return block instanceof net.minecraft.world.level.block.VineBlock || block == Blocks.CAVE_VINES
            || block == Blocks.CAVE_VINES_PLANT || block == Blocks.WEEPING_VINES || block == Blocks.WEEPING_VINES_PLANT
            || block == Blocks.TWISTING_VINES || block == Blocks.TWISTING_VINES_PLANT || block == Blocks.HANGING_ROOTS
            || block == Blocks.PALE_HANGING_MOSS || block == Blocks.LILY_PAD || block == Blocks.SUGAR_CANE
            || block == Blocks.KELP || block == Blocks.KELP_PLANT || block == Blocks.BIG_DRIPLEAF
            || block == Blocks.BIG_DRIPLEAF_STEM || block == Blocks.SPORE_BLOSSOM || block == Blocks.GLOW_LICHEN
            || block == Blocks.BAMBOO || block == Blocks.BAMBOO_SAPLING || block == Blocks.COBWEB;
    }
}
