package com.g2806.radiante.client.benchmark;

import java.util.List;

/**
 * The benchmark's camera: a fixed sequence of shots through the scene {@link BenchmarkScene} builds, each a curve the
 * eye follows while it looks at a second curve. The sequence runs by the clock, not by frames, so it lasts the same
 * and looks the same on every machine, and a faster one simply draws more frames of it.
 *
 * <p>Coordinates are the scene's own: x east, z south, y up from the ground the scene stands on, origin at its middle.
 */
final class BenchmarkPath {

    /**
     * One shot. {@code time} is the time of day it is lit by (the game's ticks: 0 sunrise, 6000 noon, 12000 sunset,
     * 18000 midnight); {@code eye} and {@code look} are the points the eye and what it looks at pass through.
     */
    record Shot(String name, int time, double seconds, double[][] eye, double[][] look) {
    }

    /** Where the eye is and where it looks, in the scene's coordinates and the game's angles. */
    record Pose(double x, double y, double z, float yaw, float pitch) {
    }

    /**
     * The cave's middle line, from its mouth on the mountain's south slope, through the chamber, out behind the
     * waterfall. The scene carves along it and the cave shot flies along it.
     */
    static final double[][] CAVE = {
        {-14, 15.5, -9}, {-14, 15.5, -14}, {-14.5, 15, -21}, {-12, 15, -28}, {-3, 15.5, -32}, {5, 15.5, -29},
        {9, 15.5, -23}, {10, 15.5, -17}, {10, 15.5, -11},
    };

    static final List<Shot> SHOTS = List.of(
        // The sun comes up behind the lake; the eye rises off the water towards the house.
        new Shot("dawn", 23350, 12,
            new double[][] {{30, 12.8, 8}, {22, 13.2, 9}, {12, 14.5, 12}, {2, 17, 15}, {-6, 19, 17}},
            new double[][] {{0, 14, 10}, {-10, 14, 12}, {-20, 15, 14}, {-26, 15, 14}, {-27, 15, 14}}),
        // Morning: from the dock past the terrace, round the house and over the field.
        new Shot("village", 1800, 13,
            new double[][] {{2, 14, 13}, {-6, 14.5, 20}, {-16, 15, 24}, {-27, 16, 24.5}, {-38, 17.5, 20},
                {-40, 18, 10}, {-34, 17, 2}},
            new double[][] {{-10, 14, 13}, {-18, 14.5, 15}, {-26, 15, 15}, {-27, 14, 22}, {-27, 14, 28},
                {-26, 15, 14}, {-26, 15, 13}}),
        // Noon: high over the east hills, down across the lake to the waterfall.
        new Shot("flyover", 6000, 13,
            new double[][] {{58, 44, 34}, {46, 40, 22}, {34, 32, 12}, {24, 24, 2}, {17, 19, -5}, {13, 17, -9}},
            new double[][] {{20, 14, 10}, {12, 14, 4}, {10, 16, -6}, {10, 20, -14}, {10, 22, -15}, {10, 20, -15}}),
        // Into the mountain: torches, ore, the lava chamber, and out through the falling water.
        new Shot("cave", 6000, 17,
            new double[][] {{-15, 16, 2}, {-14, 15.8, -5}, {-14, 15.5, -12}, {-14.5, 15.2, -21}, {-12, 15.2, -28},
                {-3, 15.8, -32}, {5, 15.8, -29}, {9, 15.6, -23}, {10, 15.5, -17}, {10, 15.5, -11}, {11, 16, -3}},
            new double[][] {{-14, 15, -12}, {-14, 15, -18}, {-14, 15, -24}, {-10, 15, -30}, {-2, 14.5, -32},
                {4, 14, -30}, {9, 15, -24}, {10, 15.5, -17}, {10, 15.5, -8}, {11, 15, 2}, {12, 14, 10}}),
        // Under the lake: kelp, sea lanterns on the bed, the surface from below, then up into the air.
        new Shot("underwater", 9000, 11,
            new double[][] {{22, 13.5, 6}, {19, 10.2, 5}, {15, 8.2, 3}, {10, 7.8, 0}, {6, 8.4, -4}, {5, 10, 2},
                {4, 13.6, 8}},
            new double[][] {{16, 9, 4}, {12, 7.5, 2}, {9, 7.5, -1}, {7, 8, -5}, {8, 11, -12}, {2, 12, 8},
                {-6, 14, 12}}),
        // Golden hour: low sun through the coloured glass of the terrace, the portal on the hill beyond.
        new Shot("sunset", 11750, 13,
            new double[][] {{4, 15, 20}, {-2, 14.6, 22}, {-9, 14.4, 21}, {-15, 14.6, 16}, {-14, 15.5, 8},
                {-6, 17, 2}, {6, 20, 2}},
            new double[][] {{-8, 14, 13}, {-8, 14, 13}, {-8, 14, 13}, {-8, 14, 13}, {-6, 14, 13}, {10, 15, 14},
                {44, 30, 30}}),
        // Night: along the lamp posts to the lit windows, then up and back for the whole valley under the moon.
        new Shot("night", 18000, 15,
            new double[][] {{-14, 14.8, -8}, {-14.5, 14.6, 0}, {-17, 14.6, 8}, {-19, 14.8, 13.5}, {-16, 16, 20},
                {-4, 22, 30}, {14, 32, 42}, {30, 42, 52}},
            new double[][] {{-15, 14.5, 2}, {-18, 14.5, 10}, {-24, 14.6, 14}, {-27, 14.6, 14}, {-26, 14.5, 14},
                {-16, 14, 8}, {-4, 14, -4}, {0, 16, -10}}));

    private BenchmarkPath() {
    }

    /** The whole sequence's length in seconds. */
    static double totalSeconds() {
        double total = 0;
        for (Shot shot : SHOTS) {
            total += shot.seconds();
        }
        return total;
    }

    /** The eye's pose {@code seconds} into {@code shot}, eased so that a shot starts and ends without a jolt. */
    static Pose pose(Shot shot, double seconds) {
        double t = Math.min(Math.max(seconds / shot.seconds(), 0.0), 1.0);
        // A gentle ease: the middle of the shot moves at an even pace, its ends are softened.
        double eased = t + 0.12 * Math.sin(2.0 * Math.PI * t) / (2.0 * Math.PI) * -1.0;
        double[] eye = spline(shot.eye(), eased);
        double[] look = spline(shot.look(), eased);
        double dx = look[0] - eye[0];
        double dy = look[1] - eye[1];
        double dz = look[2] - eye[2];
        double flat = Math.sqrt(dx * dx + dz * dz);
        // The game's yaw is 0 looking south (+z) and grows clockwise seen from above; its pitch is positive downwards.
        float yaw = (float) Math.toDegrees(Math.atan2(-dx, dz));
        float pitch = (float) -Math.toDegrees(Math.atan2(dy, Math.max(flat, 1e-6)));
        return new Pose(eye[0], eye[1], eye[2], yaw, pitch);
    }

    /** A point on the Catmull-Rom curve through {@code points}, {@code t} from 0 (the first) to 1 (the last). */
    static double[] spline(double[][] points, double t) {
        int segments = points.length - 1;
        double scaled = Math.min(Math.max(t, 0.0), 1.0) * segments;
        int i = Math.min((int) Math.floor(scaled), segments - 1);
        double u = scaled - i;
        double[] p0 = points[Math.max(i - 1, 0)];
        double[] p1 = points[i];
        double[] p2 = points[i + 1];
        double[] p3 = points[Math.min(i + 2, segments)];
        double[] out = new double[3];
        for (int axis = 0; axis < 3; axis++) {
            out[axis] = 0.5 * (2.0 * p1[axis] + (p2[axis] - p0[axis]) * u
                + (2.0 * p0[axis] - 5.0 * p1[axis] + 4.0 * p2[axis] - p3[axis]) * u * u
                + (3.0 * p1[axis] - p0[axis] - 3.0 * p2[axis] + p3[axis]) * u * u * u);
        }
        return out;
    }
}
