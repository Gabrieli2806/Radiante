package com.g2806.radiante.client;

import com.mojang.datafixers.util.Pair;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.chunk.ChunkGenerator;
import net.minecraft.world.level.levelgen.Heightmap;
import net.minecraft.world.level.levelgen.RandomState;

/**
 * {@code /radiante biome}: walks through every biome the current dimension can generate, one at a time, for checking
 * fog, water and sky colour per biome. Singleplayer only (the biome is found on the integrated server).
 *
 * <ul>
 * <li>{@code /radiante biome} or {@code /radiante biome next}: the next biome in the list, nearest place first.
 * <li>{@code /radiante biome back}: the previous one.
 * <li>{@code /radiante biome <name>}: a given biome, e.g. {@code swamp} or {@code minecraft:cherry_grove}.
 * <li>{@code /radiante biome list}: the biomes of this dimension, in the order the tour takes.
 * </ul>
 */
final class BiomeTour {

    /** How far from the player a biome is searched for, in blocks. */
    private static final int SEARCH_RADIUS = 6400;
    private static final int HORIZONTAL_STEP = 32;
    private static final int VERTICAL_STEP = 64;
    /** A place counts as inside a biome with the biome this far to each side, in blocks. */
    private static final int INSIDE_MARGIN = 28;
    /** How many rings past the first place found one inside the biome is still looked for. */
    private static final int INSIDE_EXTRA_RINGS = 24;

    private static int index = -1;

    private BiomeTour() {
    }

    /** Runs on the client thread with the arguments after {@code biome}. */
    static void handle(Minecraft minecraft, MinecraftServer server, String[] args) {
        BlockPos playerPos = minecraft.player.blockPosition();
        var dimension = minecraft.player.level().dimension();
        server.execute(() -> {
            ServerLevel level = server.getLevel(dimension);
            if (level == null) {
                return;
            }
            List<ResourceKey<Biome>> biomes = biomes(level);
            if (biomes.isEmpty()) {
                return;
            }
            String action = args.length > 0 ? args[0] : "next";
            ResourceKey<Biome> target;
            switch (action) {
                case "list" -> {
                    StringBuilder names = new StringBuilder();
                    for (int i = 0; i < biomes.size(); i++) {
                        names.append(i == 0 ? "" : ", ").append(i + 1).append(' ')
                            .append(biomes.get(i).identifier().getPath());
                    }
                    say(minecraft, Component.translatable("message.radiante.biome.list", biomes.size(),
                        names.toString()));
                    return;
                }
                case "next" -> {
                    index = (index + 1) % biomes.size();
                    target = biomes.get(index);
                }
                case "back" -> {
                    index = (index - 1 + biomes.size()) % biomes.size();
                    target = biomes.get(index);
                }
                default -> {
                    target = find(biomes, action);
                    if (target == null) {
                        say(minecraft, Component.translatable("message.radiante.biome.unknown", action));
                        return;
                    }
                    index = biomes.indexOf(target);
                }
            }
            ResourceKey<Biome> wanted = target;
            String name = wanted.identifier().toString();
            // Where the biome is the one on the surface, if it is anywhere: found by height alone it is as often the
            // ground under another one (an ocean's bed below a cold ocean's water) or a cave under it.
            BlockPos pos = findOnSurface(level, wanted, playerPos);
            if (pos == null) {
                Pair<BlockPos, Holder<Biome>> found = level.findClosestBiome3d(holder -> holder.is(wanted), playerPos,
                    SEARCH_RADIUS, HORIZONTAL_STEP, VERTICAL_STEP);
                if (found == null) {
                    say(minecraft, Component.translatable("message.radiante.biome.not_found", name, index + 1,
                        biomes.size()));
                    return;
                }
                pos = found.getFirst();
            }
            int y = level.getChunk(pos.getX() >> 4, pos.getZ() >> 4)
                .getHeight(Heightmap.Types.MOTION_BLOCKING, pos.getX() & 15, pos.getZ() & 15) + 1;
            // Underground biomes (caves) are reached where they were found, not on the surface above them. Only
            // those: a surface biome runs down through the ground under it, and is often found there first (the
            // search starts at the player's height), which put the player inside a hill.
            boolean onSurface = level.getBiome(new BlockPos(pos.getX(), y, pos.getZ())).is(wanted);
            if (level.dimensionType().hasCeiling()) {
                // The Nether: the height map is the bedrock roof. The first open floor from the middle of the
                // dimension down, or else up, is where the biome is seen.
                y = openFloor(level, pos.getX(), pos.getZ(), pos.getY());
            } else if (!onSurface && pos.getY() < y - 8 && !level.canSeeSky(pos)) {
                y = pos.getY();
            }
            server.getCommands().performPrefixedCommand(server.createCommandSourceStack().withLevel(level)
                    .withSuppressedOutput(),
                String.format(Locale.ROOT, "tp @p %d %d %d", pos.getX(), y, pos.getZ()));
            say(minecraft, Component.translatable("message.radiante.biome.at", name, index + 1, biomes.size(),
                pos.getX(), y, pos.getZ()));
        });
    }

    /**
     * The nearest column, ring by ring around {@code from}, whose surface is in the biome; null when there is none
     * within reach (a cave biome). A column with the biome all around it is looked for a little further out than
     * the first one found, which is as often on the border, where the biome's look is half its neighbour's. Worked
     * out from the generator, so no chunk is loaded for it. A roofed dimension has no surface to speak of: there the
     * column is taken at the middle of its height, and the caller finds a floor in it.
     */
    private static BlockPos findOnSurface(ServerLevel level, ResourceKey<Biome> wanted, BlockPos from) {
        ChunkGenerator generator = level.getChunkSource().getGenerator();
        RandomState random = level.getChunkSource().randomState();
        boolean roofed = level.dimensionType().hasCeiling();
        int probeY = roofed ? level.getMinY() + level.dimensionType().logicalHeight() / 2 : level.getSeaLevel() + 8;
        BlockPos onBorder = null;
        int lastRing = SEARCH_RADIUS / HORIZONTAL_STEP;
        for (int ring = 0; ring <= lastRing; ring++) {
            for (int dx = -ring; dx <= ring; dx++) {
                // The whole of the two sides at the ring's ends, and only the two corners' rows in between.
                int step = Math.abs(dx) == ring ? 1 : Math.max(2 * ring, 1);
                for (int dz = -ring; dz <= ring; dz += step) {
                    int x = from.getX() + dx * HORIZONTAL_STEP;
                    int z = from.getZ() + dz * HORIZONTAL_STEP;
                    // The cheap look first; the surface's height costs a column of terrain noise.
                    if (!level.getUncachedNoiseBiome(x >> 2, probeY >> 2, z >> 2).is(wanted)) {
                        continue;
                    }
                    boolean inside = biomeIs(level, wanted, x - INSIDE_MARGIN, probeY, z)
                        && biomeIs(level, wanted, x + INSIDE_MARGIN, probeY, z)
                        && biomeIs(level, wanted, x, probeY, z - INSIDE_MARGIN)
                        && biomeIs(level, wanted, x, probeY, z + INSIDE_MARGIN);
                    if (!inside && onBorder != null) {
                        continue;
                    }
                    int y = roofed ? probeY
                        : generator.getBaseHeight(x, z, Heightmap.Types.WORLD_SURFACE_WG, level, random);
                    if (y <= level.getMinY() + 1 || !biomeIs(level, wanted, x, y, z)) {
                        continue;
                    }
                    if (inside) {
                        return new BlockPos(x, y, z);
                    }
                    onBorder = new BlockPos(x, y, z);
                    lastRing = Math.min(lastRing, ring + INSIDE_EXTRA_RINGS);
                }
            }
        }
        return onBorder;
    }

    private static boolean biomeIs(ServerLevel level, ResourceKey<Biome> wanted, int x, int y, int z) {
        return level.getUncachedNoiseBiome(x >> 2, y >> 2, z >> 2).is(wanted);
    }

    /** The y of an open place to stand in a column of a roofed dimension, nearest below {@code from} first. */
    private static int openFloor(ServerLevel level, int x, int z, int from) {
        BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
        int bottom = level.getMinY() + 1;
        int top = level.getMinY() + level.dimensionType().logicalHeight() - 2;
        int start = Math.max(bottom, Math.min(top, Math.max(from, 70)));
        for (int pass = 0; pass < 2; pass++) {
            for (int y = start; pass == 0 ? y > bottom : y < top; y += pass == 0 ? -1 : 1) {
                if (level.getBlockState(pos.set(x, y, z)).isAir() && level.getBlockState(pos.set(x, y + 1, z)).isAir()
                    && !level.getBlockState(pos.set(x, y - 1, z)).isAir()) {
                    return y;
                }
            }
        }
        return start;
    }

    private static List<ResourceKey<Biome>> biomes(ServerLevel level) {
        List<ResourceKey<Biome>> keys = new ArrayList<>();
        for (Holder<Biome> holder : level.getChunkSource().getGenerator().getBiomeSource().possibleBiomes()) {
            holder.unwrapKey().ifPresent(keys::add);
        }
        keys.sort(Comparator.comparing(key -> key.identifier().toString()));
        return keys;
    }

    private static ResourceKey<Biome> find(List<ResourceKey<Biome>> biomes, String name) {
        for (ResourceKey<Biome> key : biomes) {
            if (key.identifier().toString().equals(name) || key.identifier().getPath().equals(name)) {
                return key;
            }
        }
        return null;
    }

    private static void say(Minecraft minecraft, Component message) {
        minecraft.execute(() -> {
            if (minecraft.player != null) {
                minecraft.player.sendSystemMessage(message);
            }
        });
    }
}
