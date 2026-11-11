package com.g2806.radiante.client.benchmark;

import com.mojang.brigadier.exceptions.CommandSyntaxException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import net.minecraft.commands.arguments.blocks.BlockStateParser;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.Registries;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;

/**
 * The benchmark's scene: a valley 160 blocks across, made from nothing but this code, so it is the same in every world
 * and on every machine. A ring of wooded hills closes it off from the world around. Inside: a lake with a dock, kelp
 * and sea lanterns on its bed; a mountain with a snow cap, a pond on its shoulder and a waterfall into the lake; a cave
 * through the mountain with every ore, amethyst, a lava pool and a way out behind the waterfall; a timber house with
 * lit windows, a hearth and a field; a terrace of polished stone, metal pillars and coloured glass; a path with lamp
 * posts; a ruined portal on a hill; trees, flowers and animals.
 *
 * <p>It is first worked out in memory, in the scene's own coordinates (x east, z south, y up from the ground it stands
 * on, origin at its middle), then written into the world without block updates: water does not flow, leaves do not
 * decay and nothing falls, so what the camera sees does not depend on how long the world has been running.
 */
final class BenchmarkScene {

    /** Raised whenever the scene changes: a world holding an older one is built again. */
    static final int VERSION = 1;
    /** The scene reaches this far from its middle along x and z. */
    static final int RADIUS = 80;
    static final int HEIGHT = 72;
    /** The lake's top water block, and the top block of the valley floor around it. */
    static final int WATER = 11;
    static final int GROUND = 12;
    static final String ENTITY_TAG = "radiante_benchmark";

    private static final int SIZE = 2 * RADIUS + 1;
    /** Put down as they are: no neighbour updates, no shape updates, no onPlace (falling, fluid ticks). */
    private static final int FLAGS = Block.UPDATE_CLIENTS | Block.UPDATE_KNOWN_SHAPE | Block.UPDATE_SKIP_ON_PLACE;

    private static final double LAKE_X = 12;
    private static final double LAKE_Z = 2;
    private static final double LAKE_RADIUS = 17;

    // What a column is, for what may grow or be built on it.
    private static final byte OPEN = 0;
    private static final byte LAKE = 1;
    private static final byte BUILT = 2;
    private static final byte ROCK = 3;

    private final short[] cells = new short[SIZE * SIZE * HEIGHT];
    private final boolean[] cave = new boolean[SIZE * SIZE * HEIGHT];
    private final int[] top = new int[SIZE * SIZE];
    private final byte[] zone = new byte[SIZE * SIZE];
    private final List<String> palette = new ArrayList<>(List.of("air"));
    private final Map<String, Short> paletteIndex = new HashMap<>(Map.of("air", (short) 0));
    private final List<String> commands = new ArrayList<>();
    private final List<int[]> trees = new ArrayList<>();

    BenchmarkScene() {
        terrain();
        cave();
        waterfall();
        lakeLife();
        paths();
        house();
        terrace();
        field();
        portal();
        trees();
        plants();
        animals();
    }

    /** The commands that put the scene's entities in place, with coordinates relative to the scene's origin. */
    List<String> commands() {
        return this.commands;
    }

    /** Writes every block into the world; {@code origin} is the scene's middle, on the ground. Server thread. */
    void place(ServerLevel level, BlockPos origin) {
        BlockState[] states = new BlockState[this.palette.size()];
        var blocks = level.holderLookup(Registries.BLOCK);
        for (int i = 0; i < states.length; i++) {
            try {
                states[i] = BlockStateParser.parseForBlock(blocks, this.palette.get(i), false).blockState();
            } catch (CommandSyntaxException e) {
                // A block this version of the game does not have: leave air, the scene stays usable.
                com.g2806.radiante.client.RadianteClient.LOGGER.warn("[benchmark] unknown block {}", this.palette.get(i));
                states[i] = Blocks.AIR.defaultBlockState();
            }
        }
        BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
        for (int y = 0; y < HEIGHT; y++) {
            for (int z = -RADIUS; z <= RADIUS; z++) {
                for (int x = -RADIUS; x <= RADIUS; x++) {
                    level.setBlock(pos.set(origin.getX() + x, origin.getY() + y, origin.getZ() + z),
                        states[this.cells[index(x, y, z)]], FLAGS);
                }
            }
        }
    }

    // ---- Cells --------------------------------------------------------------------------------------------------

    private static int index(int x, int y, int z) {
        return (y * SIZE + (z + RADIUS)) * SIZE + (x + RADIUS);
    }

    private static int column(int x, int z) {
        return (z + RADIUS) * SIZE + (x + RADIUS);
    }

    private static boolean inside(int x, int y, int z) {
        return x >= -RADIUS && x <= RADIUS && z >= -RADIUS && z <= RADIUS && y >= 0 && y < HEIGHT;
    }

    private void set(int x, int y, int z, String block) {
        if (!inside(x, y, z)) {
            return;
        }
        Short id = this.paletteIndex.get(block);
        if (id == null) {
            id = (short) this.palette.size();
            this.palette.add(block);
            this.paletteIndex.put(block, id);
        }
        this.cells[index(x, y, z)] = id;
    }

    private String get(int x, int y, int z) {
        return inside(x, y, z) ? this.palette.get(this.cells[index(x, y, z)]) : "air";
    }

    private boolean air(int x, int y, int z) {
        return inside(x, y, z) && this.cells[index(x, y, z)] == 0;
    }

    private void box(int x0, int y0, int z0, int x1, int y1, int z1, String block) {
        for (int y = y0; y <= y1; y++) {
            for (int z = z0; z <= z1; z++) {
                for (int x = x0; x <= x1; x++) {
                    set(x, y, z, block);
                }
            }
        }
    }

    private int topOf(int x, int z) {
        return x < -RADIUS || x > RADIUS || z < -RADIUS || z > RADIUS ? -1 : this.top[column(x, z)];
    }

    private byte zoneOf(int x, int z) {
        return x < -RADIUS || x > RADIUS || z < -RADIUS || z > RADIUS ? BUILT : this.zone[column(x, z)];
    }

    private void mark(int x0, int z0, int x1, int z1, byte kind) {
        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                if (x >= -RADIUS && x <= RADIUS && z >= -RADIUS && z <= RADIUS) {
                    this.zone[column(x, z)] = kind;
                }
            }
        }
    }

    // ---- Numbers ------------------------------------------------------------------------------------------------

    /** A number from 0 to 1 that depends only on its arguments. */
    private static double hash(int x, int y, int z, int salt) {
        long h = x * 0x9E3779B97F4A7C15L ^ y * 0xC2B2AE3D27D4EB4FL ^ z * 0x165667B19E3779F9L ^ salt * 0xD6E8FEB86659FD93L;
        h ^= h >>> 32;
        h *= 0xD6E8FEB86659FD93L;
        h ^= h >>> 32;
        h *= 0xD6E8FEB86659FD93L;
        h ^= h >>> 32;
        return (h >>> 11) * 0x1.0p-53;
    }

    /** Smooth noise from -1 to 1. */
    private static double noise(double x, double z, int salt) {
        int x0 = (int) Math.floor(x);
        int z0 = (int) Math.floor(z);
        double fx = x - x0;
        double fz = z - z0;
        fx = fx * fx * (3 - 2 * fx);
        fz = fz * fz * (3 - 2 * fz);
        double a = hash(x0, 0, z0, salt);
        double b = hash(x0 + 1, 0, z0, salt);
        double c = hash(x0, 0, z0 + 1, salt);
        double d = hash(x0 + 1, 0, z0 + 1, salt);
        return ((a + (b - a) * fx) * (1 - fz) + (c + (d - c) * fx) * fz) * 2 - 1;
    }

    /** 0 below {@code from}, 1 above {@code to}, smooth between. */
    private static double smooth(double from, double to, double value) {
        double t = Math.min(Math.max((value - from) / (to - from), 0), 1);
        return t * t * (3 - 2 * t);
    }

    private static double lerp(double a, double b, double t) {
        return a + (b - a) * t;
    }

    // ---- Terrain ------------------------------------------------------------------------------------------------

    /** How much of the mountain a column is: 0 valley, 1 mountain. Its south face over the lake is a sheer cliff. */
    private static double mountain(double x, double z) {
        double eastWest = smooth(-36, -25, x) * (1 - smooth(17, 28, x));
        double cliff = smooth(-7, -2, x) * (1 - smooth(16, 19, x));
        double south = lerp(1 - smooth(-25, -9, z), 1 - smooth(-14.9, -14.1, z), cliff);
        return eastWest * smooth(-60, -49, z) * south;
    }

    private static double lakeEdge(int x, int z) {
        return LAKE_RADIUS + 1.8 * noise(x / 6.0, z / 6.0, 7);
    }

    /** How flat a column is forced to be: 1 inside the rectangle, fading to 0 four blocks out. */
    private static double pad(int x, int z, int x0, int z0, int x1, int z1) {
        double dx = Math.max(Math.max(x0 - x, x - x1), 0);
        double dz = Math.max(Math.max(z0 - z, z - z1), 0);
        return 1 - smooth(0, 4, Math.sqrt(dx * dx + dz * dz));
    }

    private void terrain() {
        for (int z = -RADIUS; z <= RADIUS; z++) {
            for (int x = -RADIUS; x <= RADIUS; x++) {
                double r = Math.sqrt(x * x + z * z);
                double height = GROUND + 2.2 * noise(x / 26.0, z / 26.0, 1) + 0.9 * noise(x / 9.0, z / 9.0, 2);
                // The ring of hills that closes the valley.
                height += 16 * Math.exp(-Math.pow((r - 61) / 10, 2)) * (0.7 + 0.45 * noise(x / 15.0, z / 15.0, 3));
                // Level ground where things are built.
                double flat = Math.max(pad(x, z, -34, 7, -18, 21), Math.max(pad(x, z, -35, 24, -19, 38),
                    pad(x, z, -13, 7, -3, 19)));
                height = lerp(height, GROUND, flat);
                double m = mountain(x, z);
                double peak = 31 + 4 * noise(x / 13.0, z / 13.0, 4)
                    + 13 * Math.exp(-(Math.pow(x + 10, 2) + Math.pow(z + 40, 2)) / (2 * 9 * 9));
                height = lerp(height, peak, m);
                // Down to the ground the scene stands on at its rim.
                double rim = smooth(69, 79.5, r);
                height = lerp(height, -1.5, rim);

                int columnTop = (int) Math.round(height);
                double lake = Math.sqrt(Math.pow(x - LAKE_X, 2) + Math.pow(z - LAKE_Z, 2));
                double edge = lakeEdge(x, z);
                boolean water = lake < edge && m < 0.5;
                if (water) {
                    double bed = WATER - 6.5 * Math.pow(1 - Math.pow(lake / edge, 2), 0.7);
                    columnTop = Math.min(columnTop, (int) Math.round(bed));
                }
                this.top[column(x, z)] = columnTop;
                if (columnTop < 0) {
                    this.zone[column(x, z)] = BUILT;
                    continue;
                }

                boolean rock = m > 0.35 && (columnTop >= 22 || m < 0.98);
                String surface;
                if (water) {
                    this.zone[column(x, z)] = LAKE;
                    double pick = hash(x, 0, z, 11);
                    surface = WATER - columnTop <= 1 ? "sand" : pick < 0.45 ? "gravel" : pick < 0.8 ? "sand" : "clay";
                } else if (rock) {
                    this.zone[column(x, z)] = ROCK;
                    double pick = noise(x / 5.0, z / 5.0, 12);
                    surface = columnTop >= 39 ? "snow_block" : pick > 0.45 ? "andesite" : pick < -0.55 ? "gravel"
                        : pick < -0.35 ? "mossy_cobblestone" : "stone";
                } else if (columnTop == GROUND && lake < edge + 2.2) {
                    surface = "sand";
                } else {
                    surface = "grass_block";
                }
                for (int y = 0; y <= columnTop; y++) {
                    String block = y == columnTop ? surface : y >= columnTop - 3 && !rock
                        ? (water ? "sand" : "dirt") : "stone";
                    set(x, y, z, block);
                }
                if (water) {
                    for (int y = columnTop + 1; y <= WATER; y++) {
                        set(x, y, z, "water");
                    }
                }
            }
        }
    }

    // ---- The cave -----------------------------------------------------------------------------------------------

    private void cave() {
        double[][] line = BenchmarkPath.CAVE;
        int segments = line.length - 1;
        for (double s = 0; s <= segments; s += 0.02) {
            double[] p = BenchmarkPath.spline(line, s / segments);
            // A tunnel, opening into a chamber between the fifth and sixth points.
            double radius = 2.7 + 4.2 * Math.exp(-Math.pow((s - 4.5) / 1.1, 2));
            double floor = p[1] - (radius > 4 ? 3.0 : 1.9);
            int reach = (int) Math.ceil(radius);
            for (int dy = -reach; dy <= reach; dy++) {
                for (int dz = -reach; dz <= reach; dz++) {
                    for (int dx = -reach; dx <= reach; dx++) {
                        int x = (int) Math.floor(p[0]) + dx;
                        int y = (int) Math.floor(p[1]) + dy;
                        int z = (int) Math.floor(p[2]) + dz;
                        double ex = x + 0.5 - p[0];
                        double ey = (y + 0.5 - p[1]) * 1.2;
                        double ez = z + 0.5 - p[2];
                        if (ex * ex + ey * ey + ez * ez > radius * radius || y + 0.5 < floor || !inside(x, y, z)) {
                            continue;
                        }
                        if (y > topOf(x, z) || get(x, y, z).equals("water")) {
                            continue;
                        }
                        set(x, y, z, "air");
                        this.cave[index(x, y, z)] = true;
                        // Open to the sky where the tunnel breaks the slope: nothing grows in the cut.
                        if (y == topOf(x, z)) {
                            this.zone[column(x, z)] = ROCK;
                        }
                    }
                }
            }
        }

        // The walls: ore of every kind, deepslate around the chamber.
        String[] ores = {"coal_ore", "iron_ore", "copper_ore", "gold_ore", "redstone_ore", "lapis_ore", "diamond_ore",
            "emerald_ore"};
        double[] share = {0.2, 0.18, 0.16, 0.1, 0.1, 0.08, 0.1, 0.08};
        for (int y = 1; y < HEIGHT - 1; y++) {
            for (int z = -RADIUS + 1; z < RADIUS; z++) {
                for (int x = -RADIUS + 1; x < RADIUS; x++) {
                    String block = get(x, y, z);
                    if (this.cave[index(x, y, z)] || !(block.equals("stone") || block.equals("dirt"))) {
                        continue;
                    }
                    if (!(caveAt(x + 1, y, z) || caveAt(x - 1, y, z) || caveAt(x, y + 1, z) || caveAt(x, y - 1, z)
                        || caveAt(x, y, z + 1) || caveAt(x, y, z - 1))) {
                        continue;
                    }
                    boolean deep = Math.pow(x - 1, 2) + Math.pow(z + 31, 2) < 100;
                    double pick = hash(x, y, z, 21);
                    if (pick < 0.24) {
                        double which = hash(x, y, z, 22);
                        int ore = 0;
                        for (double sum = share[0]; ore < ores.length - 1 && which > sum; sum += share[++ore]) {
                            // Walks to the ore whose share holds the pick.
                        }
                        set(x, y, z, (deep ? "deepslate_" : "") + ores[ore]);
                    } else if (deep) {
                        set(x, y, z, pick < 0.55 ? "cobbled_deepslate" : "deepslate");
                    } else {
                        set(x, y, z, pick < 0.34 ? "andesite" : pick < 0.42 ? "tuff" : pick < 0.48 ? "cobblestone" : "stone");
                    }
                }
            }
        }

        // Amethyst in a niche off the tunnel's bend.
        for (int y = 10; y <= 20; y++) {
            for (int z = -37; z <= -27; z++) {
                for (int x = -16; x <= -6; x++) {
                    double d = Math.sqrt(Math.pow(x + 11, 2) + Math.pow(y - 15, 2) + Math.pow(z + 32, 2));
                    if (d > 4.2) {
                        continue;
                    }
                    if (!this.cave[index(x, y, z)] && !air(x, y, z) && touchesCave(x, y, z)) {
                        set(x, y, z, hash(x, y, z, 23) < 0.25 ? "budding_amethyst" : "amethyst_block");
                    } else if (this.cave[index(x, y, z)] && !caveAt(x, y - 1, z) && hash(x, y, z, 24) < 0.6) {
                        set(x, y, z, hash(x, y, z, 25) < 0.5 ? "amethyst_cluster[facing=up]" : "large_amethyst_bud[facing=up]");
                    }
                }
            }
        }

        // The chamber: a lava pool with a rim of magma, a heap of metal and gems, lanterns from the roof.
        for (int z = -37; z <= -29; z++) {
            for (int x = -5; x <= 3; x++) {
                double d = Math.sqrt(Math.pow(x + 1, 2) + Math.pow(z + 33, 2));
                int floor = caveFloor(x, z, 16);
                if (floor < 0) {
                    continue;
                }
                if (d < 2.4) {
                    set(x, floor, z, "lava");
                    set(x, floor - 1, z, "lava");
                    set(x, floor - 2, z, "magma_block");
                } else if (d < 3.4) {
                    set(x, floor, z, "magma_block");
                }
            }
        }
        String[] treasure = {"gold_block", "iron_block", "copper_block", "diamond_block", "emerald_block",
            "raw_gold_block", "redstone_block", "lapis_block", "amethyst_block"};
        int heap = 0;
        for (int z = -29; z <= -27; z++) {
            for (int x = 3; x <= 6; x++) {
                int floor = caveFloor(x, z, 16);
                if (floor < 0 || !get(x, floor + 1, z).equals("air")) {
                    continue;
                }
                set(x, floor + 1, z, treasure[heap % treasure.length]);
                if (heap % 3 == 0) {
                    set(x, floor + 2, z, treasure[(heap + 4) % treasure.length]);
                }
                heap++;
            }
        }
        hang(1, -30, "lantern[hanging=true]");
        hang(-5, -31, "lantern[hanging=true]");
        hang(6, -31, "soul_lantern[hanging=true]");
        for (int z = -36; z <= -26; z++) {
            for (int x = -8; x <= 9; x++) {
                if (hash(x, 1, z, 26) < 0.07) {
                    hang(x, z, "pointed_dripstone[vertical_direction=down,thickness=tip]");
                }
            }
        }

        // Torches along the tunnel, a few blocks apart, off to one side.
        for (double s = 0.6; s <= segments; s += 0.55) {
            if (Math.abs(s - 4.5) < 1.4) {
                continue;
            }
            double[] p = BenchmarkPath.spline(line, s / segments);
            double[] ahead = BenchmarkPath.spline(line, Math.min(s + 0.05, segments) / segments);
            double dx = ahead[0] - p[0];
            double dz = ahead[2] - p[2];
            double length = Math.max(Math.sqrt(dx * dx + dz * dz), 1e-6);
            double side = ((int) (s / 0.55) % 2 == 0 ? 1.7 : -1.7) / length;
            int x = (int) Math.floor(p[0] - dz * side);
            int z = (int) Math.floor(p[2] + dx * side);
            int floor = caveFloor(x, z, (int) p[1] + 1);
            if (floor >= 0 && get(x, floor + 1, z).equals("air")) {
                set(x, floor + 1, z, "torch");
            }
        }
    }

    private boolean caveAt(int x, int y, int z) {
        return inside(x, y, z) && this.cave[index(x, y, z)];
    }

    private boolean touchesCave(int x, int y, int z) {
        return caveAt(x + 1, y, z) || caveAt(x - 1, y, z) || caveAt(x, y + 1, z) || caveAt(x, y - 1, z)
            || caveAt(x, y, z + 1) || caveAt(x, y, z - 1);
    }

    /** The top solid block under the cave at a column, looking down from {@code from}; -1 when there is no cave. */
    private int caveFloor(int x, int z, int from) {
        for (int y = from; y > 2; y--) {
            if (caveAt(x, y, z) && !caveAt(x, y - 1, z)) {
                return y - 1;
            }
        }
        return -1;
    }

    /** Hangs a block from the cave's roof over a column, where there is one. */
    private void hang(int x, int z, String block) {
        for (int y = 12; y < 26; y++) {
            if (caveAt(x, y, z) && !caveAt(x, y + 1, z) && !air(x, y + 1, z) && get(x, y, z).equals("air")) {
                set(x, y, z, block);
                return;
            }
        }
    }

    // ---- Water --------------------------------------------------------------------------------------------------

    /** A pond on the mountain's shoulder, a channel to the cliff's edge and the water falling from it into the lake. */
    private void waterfall() {
        int level = topOf(10, -19);
        for (int z = -25; z <= -15; z++) {
            for (int x = 5; x <= 15; x++) {
                double d = Math.sqrt(Math.pow(x - 10, 2) + Math.pow((z + 20.5) * 0.8, 2));
                boolean pond = d < 3.4;
                boolean channel = x >= 8 && x <= 12 && z >= -18;
                boolean bank = d < 5 || (x >= 7 && x <= 13 && z >= -18);
                if (!bank) {
                    continue;
                }
                // Level the bank, so the water has an even rim.
                for (int y = topOf(x, z) + 1; y <= level; y++) {
                    set(x, y, z, "stone");
                }
                for (int y = level + 1; y <= topOf(x, z); y++) {
                    set(x, y, z, "air");
                }
                this.top[column(x, z)] = level;
                set(x, level, z, pond || channel ? "water" : hash(x, 2, z, 31) < 0.5 ? "mossy_cobblestone" : "stone");
                if (pond || channel) {
                    set(x, level - 1, z, "gravel");
                }
                this.zone[column(x, z)] = ROCK;
            }
        }
        for (int x = 8; x <= 12; x++) {
            for (int y = WATER + 1; y <= level; y++) {
                set(x, y, -14, "water[level=8]");
            }
            set(x, WATER, -14, "water");
        }
    }

    private void lakeLife() {
        int[][] lanterns = {{12, 2}, {17, -3}, {7, 7}, {15, 9}, {8, -6}, {20, 4}};
        for (int[] lantern : lanterns) {
            set(lantern[0], topOf(lantern[0], lantern[1]), lantern[1], "sea_lantern");
        }
        for (int z = -RADIUS; z <= RADIUS; z++) {
            for (int x = -RADIUS; x <= RADIUS; x++) {
                if (zoneOf(x, z) != LAKE) {
                    if (zoneOf(x, z) == OPEN && get(x, topOf(x, z), z).equals("sand") && nextToWater(x, z)
                        && hash(x, 3, z, 41) < 0.22) {
                        int tall = 2 + (int) (hash(x, 4, z, 42) * 2);
                        for (int y = 1; y <= tall; y++) {
                            set(x, GROUND + y, z, "sugar_cane");
                        }
                        this.zone[column(x, z)] = BUILT;
                    }
                    continue;
                }
                int bed = topOf(x, z);
                int depth = WATER - bed;
                if (get(x, bed, z).equals("sea_lantern") || !get(x, bed + 1, z).equals("water")) {
                    continue;
                }
                double pick = hash(x, 5, z, 43);
                if (depth <= 2 && pick < 0.1 && get(x, WATER, z).equals("water")) {
                    set(x, WATER + 1, z, "lily_pad");
                } else if (depth >= 4 && pick < 0.07) {
                    int tall = 2 + (int) (hash(x, 6, z, 44) * (depth - 3));
                    for (int y = 1; y < tall; y++) {
                        set(x, bed + y, z, "kelp_plant");
                    }
                    set(x, bed + tall, z, "kelp[age=25]");
                } else if (depth >= 2 && pick < 0.2) {
                    set(x, bed + 1, z, "seagrass");
                } else if (depth >= 3 && pick < 0.25) {
                    set(x, bed + 1, z, "tall_seagrass[half=lower]");
                    set(x, bed + 2, z, "tall_seagrass[half=upper]");
                } else if (depth >= 3 && pick < 0.28) {
                    set(x, bed + 1, z, "sea_pickle[pickles=" + (2 + (int) (hash(x, 7, z, 45) * 3)) + ",waterlogged=true]");
                }
            }
        }
    }

    private boolean nextToWater(int x, int z) {
        return zoneOf(x + 1, z) == LAKE || zoneOf(x - 1, z) == LAKE || zoneOf(x, z + 1) == LAKE || zoneOf(x, z - 1) == LAKE;
    }

    // ---- What is built ------------------------------------------------------------------------------------------

    /** A path of trodden earth along a line of points, with a lamp post beside it every few blocks. */
    private void path(double[][] points, boolean lamps) {
        double walked = 0;
        double nextLamp = 3;
        int side = 1;
        for (int i = 0; i + 1 < points.length; i++) {
            double dx = points[i + 1][0] - points[i][0];
            double dz = points[i + 1][1] - points[i][1];
            double length = Math.sqrt(dx * dx + dz * dz);
            for (double step = 0; step <= length; step += 0.25) {
                double px = points[i][0] + dx * step / length;
                double pz = points[i][1] + dz * step / length;
                for (int oz = -1; oz <= 1; oz++) {
                    for (int ox = -1; ox <= 1; ox++) {
                        int x = (int) Math.floor(px) + ox;
                        int z = (int) Math.floor(pz) + oz;
                        if (Math.pow(x + 0.5 - px, 2) + Math.pow(z + 0.5 - pz, 2) > 1.3 || zoneOf(x, z) != OPEN) {
                            continue;
                        }
                        String block = get(x, topOf(x, z), z);
                        if (block.equals("grass_block") || block.equals("sand")) {
                            set(x, topOf(x, z), z, hash(x, 8, z, 51) < 0.12 ? "coarse_dirt" : "dirt_path");
                            this.zone[column(x, z)] = BUILT;
                        }
                    }
                }
                if (lamps && walked + step >= nextLamp) {
                    int x = (int) Math.floor(px - dz / length * 2.4 * side);
                    int z = (int) Math.floor(pz + dx / length * 2.4 * side);
                    if (zoneOf(x, z) == OPEN) {
                        int base = topOf(x, z);
                        set(x, base + 1, z, "cobblestone_wall");
                        set(x, base + 2, z, "oak_fence");
                        set(x, base + 3, z, "oak_fence");
                        set(x, base + 4, z, "lantern[hanging=false]");
                        this.zone[column(x, z)] = BUILT;
                    }
                    nextLamp += 9;
                    side = -side;
                }
            }
            walked += length;
        }
    }

    private void paths() {
        // From the door to the cave's mouth, to the terrace and the dock, and to the field.
        path(new double[][] {{-19.5, 14.5}, {-17, 9}, {-15.5, 2}, {-14.5, -5}, {-14, -11}}, true);
        path(new double[][] {{-19.5, 14.5}, {-13.5, 13.5}}, false);
        path(new double[][] {{-19.5, 14.5}, {-21, 20}, {-26.5, 23.5}}, false);
        // The dock: planks on posts out over the water, a lantern at its end.
        for (int x = -3; x <= 6; x++) {
            for (int z = 12; z <= 14; z++) {
                if (zoneOf(x, z) == LAKE || x <= -2) {
                    set(x, GROUND, z, "spruce_slab[type=top]");
                    if (zoneOf(x, z) != LAKE) {
                        this.zone[column(x, z)] = BUILT;
                    }
                }
            }
        }
        for (int x = -1; x <= 6; x += 3) {
            for (int z = 12; z <= 14; z += 2) {
                for (int y = topOf(x, z) + 1; y < GROUND; y++) {
                    set(x, y, z, "spruce_log[axis=y]");
                }
            }
        }
        set(6, GROUND + 1, 12, "spruce_fence");
        set(6, GROUND + 2, 12, "lantern[hanging=false]");
        set(6, GROUND + 1, 14, "barrel[facing=up]");
    }

    private void house() {
        int x0 = -31;
        int x1 = -21;
        int z0 = 10;
        int z1 = 18;
        int floor = GROUND;
        mark(x0 - 2, z0 - 2, x1 + 2, z1 + 2, BUILT);
        box(x0 - 1, floor, z0 - 1, x1 + 1, floor, z1 + 1, "cobblestone");
        box(x0, floor, z0, x1, floor, z1, "spruce_planks");
        box(x0 - 1, floor + 1, z0 - 1, x1 + 1, floor + 12, z1 + 1, "air");

        // Walls: a stone footing, plaster above, a dark timber frame.
        for (int y = floor + 1; y <= floor + 4; y++) {
            String wall = y == floor + 1 ? "cobblestone" : "white_concrete";
            for (int x = x0; x <= x1; x++) {
                set(x, y, z0, wall);
                set(x, y, z1, wall);
            }
            for (int z = z0; z <= z1; z++) {
                set(x0, y, z, wall);
                set(x1, y, z, wall);
            }
        }
        for (int y = floor + 1; y <= floor + 4; y++) {
            for (int[] post : new int[][] {{x0, z0}, {x0, z1}, {x1, z0}, {x1, z1}, {x0 + 5, z0}, {x0 + 5, z1}}) {
                set(post[0], y, post[1], "stripped_dark_oak_log[axis=y]");
            }
        }
        for (int x = x0; x <= x1; x++) {
            set(x, floor + 5, z0, "stripped_dark_oak_log[axis=x]");
            set(x, floor + 5, z1, "stripped_dark_oak_log[axis=x]");
        }
        for (int z = z0; z <= z1; z++) {
            set(x0, floor + 5, z, "stripped_dark_oak_log[axis=z]");
            set(x1, floor + 5, z, "stripped_dark_oak_log[axis=z]");
            set(x0 + 5, floor + 5, z, "stripped_dark_oak_log[axis=z]");
        }

        // Windows on every side, and the door towards the lake.
        for (int y = floor + 2; y <= floor + 3; y++) {
            for (int x : new int[] {x0 + 2, x0 + 3, x0 + 7, x0 + 8}) {
                set(x, y, z0, "glass");
                set(x, y, z1, "glass");
            }
            for (int z : new int[] {z0 + 3, z0 + 4, z0 + 5}) {
                set(x0, y, z, y == floor + 2 && z == z0 + 4 ? "white_concrete" : "glass");
            }
            set(x1, y, z0 + 2, "glass");
            set(x1, y, z1 - 2, "glass");
        }
        set(x1, floor + 1, z0 + 4, "oak_door[half=lower,facing=west,hinge=left,open=false]");
        set(x1, floor + 2, z0 + 4, "oak_door[half=upper,facing=west,hinge=left,open=false]");
        set(x1, floor + 3, z0 + 4, "white_concrete");

        // The roof: two slopes of dark stairs up to a ridge, the gables closed with planks.
        for (int k = 0; k <= 4; k++) {
            for (int x = x0 - 1; x <= x1 + 1; x++) {
                set(x, floor + 5 + k, z0 - 1 + k, "dark_oak_stairs[facing=south,half=bottom,shape=straight]");
                set(x, floor + 5 + k, z1 + 1 - k, "dark_oak_stairs[facing=north,half=bottom,shape=straight]");
            }
            for (int z = z0 + k; z <= z1 - k; z++) {
                if (k > 0) {
                    set(x0, floor + 5 + k, z, "spruce_planks");
                    set(x1, floor + 5 + k, z, "spruce_planks");
                }
            }
        }
        for (int x = x0 - 1; x <= x1 + 1; x++) {
            set(x, floor + 10, z0 + 4, "dark_oak_slab[type=bottom]");
        }

        // The hearth on the west wall, its chimney through the roof.
        box(x0, floor + 1, z0 + 3, x0, floor + 12, z0 + 5, "bricks");
        set(x0 + 1, floor + 1, z0 + 3, "bricks");
        set(x0 + 1, floor + 1, z0 + 5, "bricks");
        set(x0 + 1, floor + 2, z0 + 3, "bricks");
        set(x0 + 1, floor + 2, z0 + 5, "bricks");
        box(x0 + 1, floor + 3, z0 + 3, x0 + 1, floor + 3, z0 + 5, "brick_slab[type=bottom]");
        set(x0 + 1, floor + 1, z0 + 4, "campfire[lit=true,signal_fire=false,facing=east]");
        set(x0, floor + 13, z0 + 4, "campfire[lit=true,signal_fire=true,facing=east]");

        // Inside: a lantern from the beam, books, a bed, a table with candles, a lit furnace.
        set(x0 + 5, floor + 4, z0 + 4, "lantern[hanging=true]");
        box(x0 + 2, floor + 1, z0 + 1, x0 + 4, floor + 2, z0 + 1, "bookshelf");
        set(x0 + 3, floor + 3, z0 + 1, "candle[candles=3,lit=true]");
        set(x1 - 1, floor + 1, z0 + 1, "crafting_table");
        set(x1 - 2, floor + 1, z0 + 1, "furnace[facing=south,lit=true]");
        set(x1 - 3, floor + 1, z0 + 1, "barrel[facing=up]");
        set(x1 - 1, floor + 1, z1 - 1, "red_bed[part=foot,facing=west]");
        set(x1 - 2, floor + 1, z1 - 1, "red_bed[part=head,facing=west]");
        box(x0 + 4, floor + 1, z0 + 3, x0 + 6, floor + 1, z0 + 5, "red_carpet");
        set(x0 + 3, floor + 1, z1 - 1, "oak_fence");
        set(x0 + 3, floor + 2, z1 - 1, "oak_pressure_plate");
        set(x0 + 2, floor + 1, z1 - 1, "oak_stairs[facing=west,half=bottom,shape=straight]");
        set(x0 + 4, floor + 1, z1 - 1, "oak_stairs[facing=east,half=bottom,shape=straight]");
        set(x0 + 7, floor + 1, z1 - 1, "potted_cornflower");

        // Outside the door: a step, and a lantern either side.
        box(x1 + 1, floor + 1, z0 + 3, x1 + 1, floor + 1, z0 + 5, "spruce_slab[type=bottom]");
        for (int z : new int[] {z0 + 2, z0 + 6}) {
            set(x1 + 1, floor + 1, z, "spruce_fence");
            set(x1 + 1, floor + 2, z, "lantern[hanging=false]");
        }
        this.commands.add(String.format(Locale.ROOT,
            "summon armor_stand ~%d.5 ~%d ~%d.5 {Tags:[\"%s\"],Rotation:[200f,0f],ShowArms:1b,NoGravity:1b,"
                + "equipment:{head:{id:\"minecraft:iron_helmet\"},chest:{id:\"minecraft:iron_chestplate\"},"
                + "legs:{id:\"minecraft:iron_leggings\"},feet:{id:\"minecraft:iron_boots\"},"
                + "mainhand:{id:\"minecraft:diamond_sword\"}}}", x1 - 2, floor + 1, z0 + 6, ENTITY_TAG));
    }

    /** A floor of polished stone by the lake, metal pillars, a roof of coloured glass and a small fountain. */
    private void terrace() {
        int x0 = -12;
        int x1 = -4;
        int z0 = 8;
        int z1 = 18;
        mark(x0 - 1, z0 - 1, x1 + 1, z1 + 1, BUILT);
        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                for (int y = topOf(x, z) + 1; y < GROUND; y++) {
                    set(x, y, z, "stone_bricks");
                }
                set(x, GROUND, z, (x + z & 1) == 0 ? "smooth_quartz" : "polished_blackstone");
                box(x, GROUND + 1, z, x, GROUND + 8, z, "air");
            }
        }
        String[] metal = {"iron_block", "gold_block", "copper_block", "waxed_oxidized_copper"};
        int pillar = 0;
        for (int x : new int[] {x0 + 1, x1 - 1}) {
            for (int z : new int[] {z0 + 1, z1 - 1}) {
                box(x, GROUND + 1, z, x, GROUND + 4, z, metal[pillar++]);
            }
        }
        String[] glass = {"red", "orange", "yellow", "lime", "light_blue", "magenta"};
        for (int z = z0 + 1; z <= z1 - 1; z++) {
            for (int x = x0 + 1; x <= x1 - 1; x++) {
                set(x, GROUND + 5, z, glass[Math.floorMod(x + z, glass.length)] + "_stained_glass");
            }
        }
        // A screen of clear, tinted and frosted glass on the side away from the lake.
        for (int z = z0 + 3; z <= z1 - 3; z++) {
            for (int y = GROUND + 1; y <= GROUND + 3; y++) {
                set(x0, y, z, z % 3 == 0 ? "tinted_glass" : z % 3 == 1 ? "glass" : "white_stained_glass");
            }
        }
        // The fountain: a basin of water round a pillar with a sea lantern and an end rod.
        int fx = -8;
        int fz = 13;
        for (int z = fz - 2; z <= fz + 2; z++) {
            for (int x = fx - 2; x <= fx + 2; x++) {
                boolean rim = Math.abs(x - fx) == 2 || Math.abs(z - fz) == 2;
                set(x, GROUND, z, rim ? "chiseled_stone_bricks" : "prismarine_bricks");
                set(x, GROUND + 1, z, rim ? "stone_brick_slab[type=bottom]" : "water");
            }
        }
        set(fx, GROUND + 1, fz, "chiseled_quartz_block");
        set(fx, GROUND + 2, fz, "sea_lantern");
        set(fx, GROUND + 3, fz, "end_rod[facing=up]");
        set(fx, GROUND + 4, z0 + 2, "soul_lantern[hanging=true]");
        set(fx, GROUND + 4, z1 - 2, "lantern[hanging=true]");
        set(x1, GROUND + 1, z0, "candle[candles=4,lit=true]");
        set(x1, GROUND + 1, z1, "candle[candles=4,lit=true]");
        // Ice and a block of every tinted glass set in the floor's edge towards the water.
        for (int z = z0 + 2; z <= z1 - 2; z++) {
            set(x1, GROUND, z, z % 2 == 0 ? "blue_ice" : "packed_ice");
        }
    }

    private void field() {
        int x0 = -33;
        int x1 = -21;
        int z0 = 26;
        int z1 = 36;
        mark(x0 - 2, z0 - 2, x1 + 2, z1 + 2, BUILT);
        String[] crops = {"wheat[age=7]", "wheat[age=7]", "carrots[age=7]", "potatoes[age=7]", "beetroots[age=3]",
            "wheat[age=6]"};
        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                for (int y = topOf(x, z) + 1; y < GROUND; y++) {
                    set(x, y, z, "dirt");
                }
                box(x, GROUND + 1, z, x, GROUND + 5, z, "air");
                if (x == -27) {
                    set(x, GROUND, z, "water");
                    continue;
                }
                set(x, GROUND, z, "farmland[moisture=7]");
                set(x, GROUND + 1, z, crops[Math.floorMod(x, crops.length)]);
            }
        }
        // A fence all round, joined by hand since nothing updates its shape, with a gap towards the house.
        for (int x = x0 - 1; x <= x1 + 1; x++) {
            String joined = "oak_fence[east=" + (x <= x1) + ",west=" + (x >= x0) + ",north=false,south=false]";
            for (int z : new int[] {z0 - 1, z1 + 1}) {
                if (z == z0 - 1 && (x == -26 || x == -25)) {
                    continue;
                }
                String corner = joined.replace("north=false", "north=" + (z == z1 + 1 && (x == x0 - 1 || x == x1 + 1)))
                    .replace("south=false", "south=" + (z == z0 - 1 && (x == x0 - 1 || x == x1 + 1)));
                set(x, GROUND + 1, z, corner);
            }
        }
        for (int z = z0; z <= z1; z++) {
            set(x0 - 1, GROUND + 1, z, "oak_fence[north=true,south=true]");
            set(x1 + 1, GROUND + 1, z, "oak_fence[north=true,south=true]");
        }
        // A scarecrow, a stack of hay and a few pumpkins.
        set(x1 - 1, GROUND + 1, z1, "oak_fence");
        set(x1 - 1, GROUND + 2, z1, "hay_block");
        set(x1 - 1, GROUND + 3, z1, "carved_pumpkin[facing=north]");
        box(x0 - 4, GROUND + 1, z0, x0 - 3, GROUND + 1, z0 + 1, "hay_block");
        set(x0 - 3, GROUND + 2, z0, "hay_block");
        set(x1 + 3, GROUND + 1, z1 - 1, "pumpkin");
        set(x1 + 4, GROUND + 1, z1 + 1, "pumpkin");
        set(x1 + 3, GROUND + 1, z1 + 2, "melon");
        mark(x0 - 4, z0, x0 - 3, z0 + 1, BUILT);
        mark(x1 + 3, z1 - 1, x1 + 4, z1 + 2, BUILT);
    }

    /** A portal on the eastern hill, half fallen, with the nether creeping out around it. */
    private void portal() {
        int px = 46;
        int pz = 33;
        int base = topOf(px, pz);
        mark(px - 4, pz - 4, px + 5, pz + 4, BUILT);
        for (int z = pz - 3; z <= pz + 3; z++) {
            for (int x = px - 3; x <= px + 4; x++) {
                for (int y = topOf(x, z) + 1; y <= base; y++) {
                    set(x, y, z, "stone");
                }
                box(x, base + 1, z, x, base + 8, z, "air");
                double pick = hash(x, 9, z, 61);
                set(x, base, z, pick < 0.4 ? "netherrack" : pick < 0.55 ? "magma_block" : pick < 0.75 ? "blackstone"
                    : "polished_blackstone_bricks");
            }
        }
        for (int x = px - 1; x <= px + 2; x++) {
            for (int y = base + 1; y <= base + 5; y++) {
                boolean frame = x == px - 1 || x == px + 2 || y == base + 1 || y == base + 5;
                set(x, y, pz, frame ? (hash(x, y, pz, 62) < 0.3 ? "crying_obsidian" : "obsidian") : "nether_portal[axis=x]");
            }
        }
        set(px + 4, base + 1, pz + 2, "gold_block");
        set(px - 3, base + 1, pz - 2, "obsidian");
        set(px - 3, base + 2, pz - 2, "crying_obsidian");
        set(px + 3, base + 1, pz - 3, "netherrack");
        set(px + 3, base + 2, pz - 3, "soul_torch");
        set(px - 2, base + 1, pz + 3, "crimson_fungus");
        set(px + 1, base + 1, pz + 3, "warped_fungus");
    }

    // ---- What grows ---------------------------------------------------------------------------------------------

    private void leaves(int x, int y, int z, String block) {
        if (air(x, y, z)) {
            set(x, y, z, block);
        }
    }

    private void canopy(int x, int y, int z, int radius, String block, int salt) {
        for (int dz = -radius; dz <= radius; dz++) {
            for (int dx = -radius; dx <= radius; dx++) {
                boolean corner = Math.abs(dx) == radius && Math.abs(dz) == radius;
                if (corner && (radius < 2 || hash(x + dx, y, z + dz, salt) < 0.6)) {
                    continue;
                }
                leaves(x + dx, y, z + dz, block);
            }
        }
    }

    private boolean roomForTree(int x, int z, int spacing) {
        for (int[] tree : this.trees) {
            if (Math.abs(tree[0] - x) < spacing && Math.abs(tree[1] - z) < spacing) {
                return false;
            }
        }
        for (int dz = -2; dz <= 2; dz++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (zoneOf(x + dx, z + dz) == BUILT || zoneOf(x + dx, z + dz) == LAKE) {
                    return false;
                }
            }
        }
        return true;
    }

    private void tree(int x, int z, String kind) {
        int base = topOf(x, z);
        this.trees.add(new int[] {x, z});
        this.zone[column(x, z)] = BUILT;
        set(x, base, z, "dirt");
        String log = kind + "_log[axis=y]";
        String leaf = kind + "_leaves[persistent=true]";
        int height = (kind.equals("birch") ? 5 : kind.equals("spruce") ? 7 : 4) + (int) (hash(x, 10, z, 71) * 3);
        for (int y = 1; y <= height; y++) {
            set(x, base + y, z, log);
        }
        int crown = base + height;
        if (kind.equals("spruce")) {
            leaves(x, crown + 2, z, leaf);
            canopy(x, crown + 1, z, 1, leaf, 72);
            for (int layer = 0; layer < height - 2; layer++) {
                canopy(x, crown - layer, z, layer % 2 == 0 ? 1 + layer / 3 : 2 + layer / 3, leaf, 73);
            }
        } else if (kind.equals("cherry")) {
            canopy(x, crown - 1, z, 3, leaf, 74);
            canopy(x, crown, z, 3, leaf, 75);
            canopy(x, crown + 1, z, 2, leaf, 76);
            for (int dz = -3; dz <= 3; dz++) {
                for (int dx = -3; dx <= 3; dx++) {
                    if (hash(x + dx, 11, z + dz, 77) < 0.3 && zoneOf(x + dx, z + dz) == OPEN
                        && get(x + dx, topOf(x + dx, z + dz), z + dz).equals("grass_block")) {
                        leaves(x + dx, topOf(x + dx, z + dz) + 1, z + dz,
                            "pink_petals[flower_amount=" + (1 + (int) (hash(x + dx, 12, z + dz, 78) * 4)) + ",facing=north]");
                    }
                }
            }
        } else {
            canopy(x, crown - 2, z, 2, leaf, 79);
            canopy(x, crown - 1, z, 2, leaf, 80);
            canopy(x, crown, z, 1, leaf, 81);
            canopy(x, crown + 1, z, 1, leaf, 82);
        }
    }

    private void trees() {
        // The great oak beside the house.
        int gx = -38;
        int gz = 5;
        if (roomForTree(gx, gz, 4)) {
            int base = Math.min(Math.min(topOf(gx, gz), topOf(gx + 1, gz)), Math.min(topOf(gx, gz + 1), topOf(gx + 1, gz + 1)));
            this.trees.add(new int[] {gx, gz});
            mark(gx - 1, gz - 1, gx + 2, gz + 2, BUILT);
            box(gx, base + 1, gz, gx + 1, base + 11, gz + 1, "dark_oak_log[axis=y]");
            for (int dy = -4; dy <= 4; dy++) {
                for (int dz = -6; dz <= 6; dz++) {
                    for (int dx = -6; dx <= 6; dx++) {
                        double d = Math.sqrt(dx * dx + dy * dy * 2.2 + dz * dz) + 1.4 * hash(gx + dx, dy, gz + dz, 83);
                        if (d < 6.2) {
                            leaves(gx + dx, base + 11 + dy, gz + dz, "dark_oak_leaves[persistent=true]");
                        }
                    }
                }
            }
        }
        for (int z = -RADIUS + 3; z <= RADIUS - 3; z++) {
            for (int x = -RADIUS + 3; x <= RADIUS - 3; x++) {
                byte kind = zoneOf(x, z);
                int height = topOf(x, z);
                if (height < GROUND || height > 36 || (kind != OPEN && kind != ROCK)) {
                    continue;
                }
                double r = Math.sqrt(x * x + z * z);
                double lake = Math.sqrt(Math.pow(x - LAKE_X, 2) + Math.pow(z - LAKE_Z, 2));
                double pick = hash(x, 13, z, 84);
                String species;
                double chance;
                if (kind == ROCK) {
                    species = "spruce";
                    chance = get(x, height, z).equals("snow_block") || this.cave[index(x, height, z)] ? 0 : 0.035;
                } else if (lake < LAKE_RADIUS + 9) {
                    species = "cherry";
                    chance = 0.022;
                } else if (r > 46) {
                    species = hash(x, 14, z, 85) < 0.3 ? "birch" : "oak";
                    chance = 0.06;
                } else {
                    species = hash(x, 14, z, 85) < 0.5 ? "birch" : "oak";
                    chance = 0.012;
                }
                if (pick < chance && roomForTree(x, z, species.equals("cherry") ? 8 : 5)) {
                    tree(x, z, species);
                }
            }
        }
    }

    private void plants() {
        String[] flowers = {"poppy", "dandelion", "cornflower", "oxeye_daisy", "azure_bluet", "allium", "red_tulip",
            "pink_tulip", "lily_of_the_valley"};
        String[] tall = {"tall_grass", "large_fern", "rose_bush", "peony", "lilac", "sunflower"};
        for (int z = -RADIUS; z <= RADIUS; z++) {
            for (int x = -RADIUS; x <= RADIUS; x++) {
                int y = topOf(x, z) + 1;
                if (zoneOf(x, z) != OPEN || !get(x, y - 1, z).equals("grass_block") || !air(x, y, z)) {
                    continue;
                }
                double pick = hash(x, 15, z, 91);
                // Meadows of flowers in patches, grass everywhere.
                double meadow = noise(x / 11.0, z / 11.0, 92);
                if (pick < 0.24) {
                    set(x, y, z, hash(x, 16, z, 93) < 0.15 ? "fern" : "short_grass");
                } else if (pick < 0.24 + (meadow > 0.2 ? 0.12 : 0.02)) {
                    set(x, y, z, flowers[(int) (hash(x, 17, z, 94) * flowers.length)]);
                } else if (pick > 0.96 && air(x, y + 1, z) && meadow < 0.5 && !nearBuilt(x, z)) {
                    String plant = tall[(int) (Math.pow(hash(x, 18, z, 95), 2.2) * tall.length)];
                    set(x, y, z, plant + "[half=lower]");
                    set(x, y + 1, z, plant + "[half=upper]");
                } else if (pick > 0.995) {
                    set(x, y, z, "sweet_berry_bush[age=3]");
                }
            }
        }
    }

    /** Whether something built stands within three blocks: tall plants keep clear of paths and walls. */
    private boolean nearBuilt(int x, int z) {
        for (int dz = -3; dz <= 3; dz++) {
            for (int dx = -3; dx <= 3; dx++) {
                if (zoneOf(x + dx, z + dz) == BUILT) {
                    return true;
                }
            }
        }
        return false;
    }

    private void animal(String type, int x, int z, int yaw, String extra) {
        this.commands.add(String.format(Locale.ROOT,
            "summon %s ~%d.5 ~%d ~%d.5 {Tags:[\"%s\"],NoAI:1b,Silent:1b,Invulnerable:1b,PersistenceRequired:1b,"
                + "Rotation:[%df,0f]%s}", type, x, topOf(x, z) + 1, z, ENTITY_TAG, yaw, extra));
    }

    private void animals() {
        animal("cow", -6, 28, 30, "");
        animal("cow", -2, 32, 200, "");
        animal("sheep", 4, 30, 120, ",Color:0b");
        animal("sheep", -10, 34, 300, ",Color:6b");
        animal("chicken", -18, 22, 80, "");
        animal("villager", -17, 11, 250, "");
        animal("horse", -40, 16, 100, "");
        for (int[] squid : new int[][] {{12, 7, 2}, {17, 8, -2}, {8, 8, 5}}) {
            this.commands.add(String.format(Locale.ROOT,
                "summon glow_squid ~%d.5 ~%d ~%d.5 {Tags:[\"%s\"],NoAI:1b,Silent:1b,Invulnerable:1b,"
                    + "PersistenceRequired:1b,NoGravity:1b}", squid[0], squid[1], squid[2], ENTITY_TAG));
        }
        this.commands.add(String.format(Locale.ROOT,
            "summon oak_boat ~20.5 ~%d ~9.5 {Tags:[\"%s\"],Rotation:[40f,0f],NoGravity:1b,Invulnerable:1b}",
            WATER + 1, ENTITY_TAG));
    }
}
