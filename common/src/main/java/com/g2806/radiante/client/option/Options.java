package com.g2806.radiante.client.option;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.proxy.vulkan.TextureProxy;
import com.g2806.radiante.client.proxy.world.ChunkProxy;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Properties;
import java.util.function.BooleanSupplier;
import java.util.function.Consumer;
import java.util.function.IntConsumer;
import java.util.function.IntSupplier;
import java.util.function.Supplier;

public class Options {

    public static final String OPTION_PROPERTIES = "options.properties";

    public static int chunkBuildingBatchSize = 12;
    public static int chunkBuildingTotalBatches = 12;
    public static int chunkBuildingThreads = getDefaultChunkBuildingThreads();
    public static boolean collectChunkEmission = true;
    /**
     * Light From Glowing Pixels: a light block casts its light from the parts of its texture that glow, in finer
     * cells shrunk to those texels (EmissionTiles), instead of from 4 x 4 cells of the whole block. Off by default:
     * more, smaller lights.
     */
    public static boolean glowingPixelLight = false;
    /** How far from the camera glowing dropped items and item frames still cast light, in blocks. */
    public static int entityLightReach = 64;
    /**
     * Radiante's diagnostics in the log, the renderer's included. On by default so a player's latest.log already says
     * what went wrong when they report a problem; the option turns it off.
     */
    public static boolean debugLogging = true;
    /**
     * Each biome's own air and water (BiomeAmbiance): a faint haze in the biome's tone and water that is clear by a
     * beach and dark in the deep ocean. On its own switch, apart from the volumetric fog, and off by default. (Stored
     * under a new name than the "biomeFog" of before 1.0, which was off by default and far heavier.)
     */
    public static boolean biomeAmbiance = false;
    /** Whether the settings screen shows the Advanced options; their values apply either way. */
    public static boolean advancedSettings = false;
    /**
     * Whether the player casts a shadow (and shows up in reflections) while the camera is in first person.
     * Minecraft does not draw the player at all then, so nothing of them would reach the world without this.
     */
    public static boolean firstPersonShadow = true;
    /**
     * How far the sun's path leans, in degrees: 0 is vanilla's, straight overhead from east to west; positive leans
     * it south, so noon shadows are not all straight down. The Sun Path setting's Tilted choice is
     * SUN_PATH_TILTED_DEGREES.
     */
    public static int sunPathTilt = 0;
    /**
     * How far the sun's path is turned around the vertical, in degrees: positive turns it clockwise seen from above,
     * moving sunrise from the east towards the south.
     */
    public static int sunPathAzimuth = 0;
    /** The sun's square turned around its own centre, in degrees. */
    public static int sunSpriteRotation = 0;
    /** The moon crosses the sky on the sun's path, opposite it; off, on moonPathTilt and moonPathAzimuth. */
    public static boolean moonFollowsSun = true;
    /** The moon's path lean, in degrees, as sunPathTilt: positive leans its highest point south. */
    public static int moonPathTilt = 0;
    /** The moon's path turned around the vertical, in degrees, as sunPathAzimuth. */
    public static int moonPathAzimuth = 0;
    /** The moon's square turned around its own centre, in degrees. */
    public static int moonSpriteRotation = 0;

    /** The lean of the Sun Path setting's Tilted choice, the one the old Vanilla Sun Path switch gave when off. */
    public static final int SUN_PATH_TILTED_DEGREES = 10;
    /** The steepest lean the path settings allow either way, and the widest turn of a path or a sprite. */
    public static final int MAX_PATH_TILT = 45;
    public static final int MAX_ROTATION = 180;

    /** Whether the sun follows vanilla's path: no lean, no turn. */
    public static boolean vanillaSunPath() {
        return sunPathTilt == 0 && sunPathAzimuth == 0;
    }
    /** Sun and moon sprites keep vanilla's fixed orientation instead of turning as they cross the sky. */
    public static boolean vanillaCelestialOrientation = true;
    /**
     * Surfaces pick a nearby light block and trace one shadow ray to it, instead of waiting for a random bounce to
     * find it. Much less noise from torches, lava and lamps; needs Block Emission for the list of lights.
     */
    public static boolean blockLightSampling = true;
    /** A torch, lantern or other light held in either hand lights up the surroundings. */
    public static boolean heldItemLight = true;
    /** Ground under open sky turns darker and glossy while it rains, and dries slowly after. */
    public static boolean rainWetness = true;
    /** The outline around the block under the crosshair. */
    public static boolean blockOutline = false;
    /**
     * Lets Minecraft's level render run over the traced world, with nothing of its own to draw, so that what other
     * mods draw into the world shows: schematics, selection boxes, waypoint beams, guides.
     */
    public static boolean modOverlays = true;
    /** The translucent plate behind text displays (leaderboards, holograms), as Minecraft draws it. */
    public static boolean textBackgrounds = true;
    /** Carved (parallax) resource pack faces are see-through at the outer edges of their block. */
    public static boolean parallaxTransparentEdges = false;
    /** The Pipeline.SETTINGS_DEFAULTS_VERSION the stored pipeline settings were written under; 0 for none. */
    public static int settingsDefaultsVersion = 0;
    /** Brightness of sunlight and the daytime sky, in percent of the shader pack's own. */
    public static int dayBrightness = 25;
    /** Brightness of moonlight and the night sky, in percent of the shader pack's own. */
    public static int nightBrightness = 35;
    /** Brightness of light emitting blocks and held lights, in percent. */
    public static int emissionBrightness = 12;
    /** Brightness of what the player holds - its glow and the light it casts - in percent. */
    public static int heldLightBrightness = 12;
    /** How thick the volumetric fog (the air that shows light shafts) is, in percent of the tuned values. */
    public static int volumetricFogStrength = 100;
    /**
     * How much clearer the stars get with height, in percent: at 100 they are twice as bright from a mountain top
     * as from sea level. 0 leaves them the same everywhere.
     */
    public static int starAltitude = 100;
    /** Blocks added to the height vanilla's clouds float at (the Vanilla clouds; the volumetric ones have their own). */
    public static int cloudHeightOffset = 0;
    public static final int MIN_CLOUD_HEIGHT_OFFSET = -96;
    public static final int MAX_CLOUD_HEIGHT_OFFSET = 320;
    /** How thick the biome haze is, in percent of the tuned values. */
    public static int biomeAmbianceStrength = 100;
    /**
     * HDR display output: the window becomes a 16-bit float scRGB swapchain and the traced world keeps its
     * highlights above SDR white. Needs HDR on in the operating system; the window format is picked at startup,
     * so a change takes effect after a restart.
     */
    public static boolean hdrOutput = false;
    /** HDR: the brightness of the display's brightest white, in nits. */
    public static int hdrPeakNits = 1000;
    /** HDR: the brightness SDR white (menus, text, the world's midtones) is shown at, in nits. */
    public static int hdrPaperWhiteNits = 200;
    /** HDR: tints blue what is shown as SDR (menus, text), to check the world is in HDR. Not saved. */
    public static boolean hdrDebugView = false;
    /** Frame generation (FrameGeneration), off until the player turns it on; generatedFrames says how many. */
    public static boolean frameGeneration = false;
    /** Frames DLSS generates per rendered frame: 0 is off, 1 is 2x, up to 5 for 6x. */
    public static int generatedFrames = 1;
    /** Which frame generation to use: 0 automatic (DLSS where it runs, else FSR), 1 DLSS, 2 FSR. */
    public static int frameGenerationBackend = 0;
    /** NVIDIA Reflex low latency mode. Loads Streamline at startup, so turning it on takes a restart. */
    public static boolean reflex = false;
    /**
     * The size the world is rendered and upscaled to before a final FSR 1 upscale to the window, by
     * com.g2806.radiante.client.render.OutputResolution name. NATIVE renders at the window's size; CUSTOM at
     * outputResolutionPercent of it.
     */
    public static String outputResolution = "NATIVE";
    /** The share of the window, in percent, the Custom output resolution renders at. */
    public static int outputResolutionPercent = com.g2806.radiante.client.render.OutputResolution.DEFAULT_PERCENT;

    /**
     * The cloud setting Distant Horizons switched off, kept until its override toggle is off again and the setting
     * is put back; empty when there is nothing to put back. See DistantHorizonsCompat.
     */
    public static String cloudsBeforeDistantHorizons = "";

    /** The renderer profile last applied (com.g2806.radiante.client.profile.RendererProfile), by name. */
    public static String rendererProfile = "BEDROCK_RTX_ENHANCED";

    /** Ray tracing can be switched off with a key, which hands the world back to Minecraft's own renderer. */
    public static boolean rayTracingEnabled = true;

    /**
     * Which addons' blocks in the settings screen are open (1) or folded (0), as {@code id=1,id=0}; addons not
     * listed were never opened or folded and follow the default (see SettingsLayout).
     */
    public static String addonsOpen = "";
    /** Set when the player chooses to run on OpenGL, where this renderer cannot work at all. */
    public static boolean useOpenGl = false;
    /** OpenGL for this session only, after Minecraft detected a failed start; never saved. */
    public static boolean openGlAfterFailedStart = false;

    /**
     * Half the logical cores. Section building shares the machine with the render thread and, in singleplayer,
     * the integrated server generating those same chunks; measured on a 24-thread CPU, 20 builders dropped the frame
     * rate from 120 to about 65 for several seconds after every teleport, while 4 held it above 110 and loaded the
     * area as quickly. More threads than this only take time away from rendering.
     */
    public static int getMaxChunkBuildingThreads() {
        int availableProcessors = Runtime.getRuntime().availableProcessors();
        boolean is64Bits = System.getProperty("os.arch", "").contains("64");
        return Math.max(1, is64Bits ? availableProcessors / 2 : Math.min(availableProcessors, 4));
    }

    public static int clampChunkBuildingThreads(int chunkBuildingThreads) {
        return Math.max(1, Math.min(chunkBuildingThreads, getMaxChunkBuildingThreads()));
    }

    public static int getDefaultChunkBuildingThreads() {
        return autoChunkBuilding()[0];
    }

    /**
     * Chunk building sized to the CPU: threads, sections per batch and batches in flight. The fixed 12 x 12 batches
     * that suit a 16 thread CPU starved the render and server threads on smaller ones (stutter, low frame rate while
     * chunks load), and players had to find the sliders to fix it.
     */
    public static int[] autoChunkBuilding() {
        int cpus = Runtime.getRuntime().availableProcessors();
        int[] values = cpus <= 4 ? new int[] {1, 4, 4}
            : cpus <= 8 ? new int[] {2, 6, 6}
            : cpus <= 12 ? new int[] {3, 8, 8}
            : cpus <= 16 ? new int[] {4, 12, 12}
            : new int[] {Math.min(6, cpus / 4), 12, 12};
        values[0] = clampChunkBuildingThreads(values[0]);
        return values;
    }

    /** On: the three chunk building settings follow autoChunkBuilding(); off: the player's own values. */
    public static boolean chunkBuildingAuto = true;

    /** Puts the automatic values in place (and in the renderer) while chunkBuildingAuto is on. */
    public static void applyChunkBuildingAuto() {
        if (!chunkBuildingAuto) {
            return;
        }
        int[] values = autoChunkBuilding();
        setChunkBuildingThreads(values[0], false);
        setChunkBuildingBatchSize(values[1], false);
        setChunkBuildingTotalBatches(values[2], false);
    }

    /** One saved setting: its key in options.properties, how to write it out and how to take it back. */
    private record Entry(String key, Supplier<String> value, Consumer<String> load) {
    }

    private static Entry bool(String key, BooleanSupplier value, Consumer<Boolean> load) {
        return new Entry(key, () -> String.valueOf(value.getAsBoolean()), text -> load.accept(Boolean.parseBoolean(text)));
    }

    private static Entry number(String key, IntSupplier value, IntConsumer load) {
        return new Entry(key, () -> String.valueOf(value.getAsInt()), text -> load.accept(Integer.parseInt(text.trim())));
    }

    /**
     * Every saved setting, in file order. Reading and writing both go through this list, so a setting is either
     * in it - and round-trips - or not saved at all; nothing can be written and never read back again.
     */
    private static final List<Entry> ENTRIES = List.of(
        number("settingsDefaultsVersion", () -> settingsDefaultsVersion, v -> settingsDefaultsVersion = v),
        number("chunkBuildingBatchSize", () -> chunkBuildingBatchSize, v -> setChunkBuildingBatchSize(v, false)),
        number("chunkBuildingTotalBatches", () -> chunkBuildingTotalBatches,
            v -> setChunkBuildingTotalBatches(v, false)),
        number("chunkBuildingThreads", () -> chunkBuildingThreads, v -> setChunkBuildingThreads(v, false)),
        bool("chunkBuildingAuto", () -> chunkBuildingAuto, v -> chunkBuildingAuto = v),
        bool("collectChunkEmission", () -> collectChunkEmission, v -> setCollectChunkEmission(v, false)),
        bool("glowingPixelLight", () -> glowingPixelLight, v -> glowingPixelLight = v),
        number("entityLightReach", () -> entityLightReach, v -> setEntityLightReach(v, false)),
        bool("debugLogging", () -> debugLogging, v -> setDebugLogging(v, false)),
        bool("advancedSettings", () -> advancedSettings, v -> advancedSettings = v),
        bool("biomeAmbiance", () -> biomeAmbiance, v -> biomeAmbiance = v),
        number("starAltitude", () -> starAltitude, v -> starAltitude = Math.max(0, Math.min(400, v))),
        number("cloudHeightOffset", () -> cloudHeightOffset,
            v -> cloudHeightOffset = Math.max(MIN_CLOUD_HEIGHT_OFFSET, Math.min(MAX_CLOUD_HEIGHT_OFFSET, v))),
        number("volumetricFogStrength", () -> volumetricFogStrength,
            v -> volumetricFogStrength = Math.max(0, Math.min(400, v))),
        number("biomeAmbianceStrength", () -> biomeAmbianceStrength,
            v -> biomeAmbianceStrength = Math.max(0, Math.min(400, v))),
        bool("firstPersonShadow", () -> firstPersonShadow, v -> firstPersonShadow = v),
        bool("heldItemLight", () -> heldItemLight, v -> heldItemLight = v),
        bool("rainWetness", () -> rainWetness, v -> rainWetness = v),
        bool("blockOutline", () -> blockOutline, v -> blockOutline = v),
        bool("modOverlays", () -> modOverlays, v -> modOverlays = v),
        bool("textBackgrounds", () -> textBackgrounds, v -> textBackgrounds = v),
        bool("parallaxTransparentEdges", () -> parallaxTransparentEdges, v -> parallaxTransparentEdges = v),
        number("dayBrightness", () -> dayBrightness, v -> dayBrightness = v),
        number("nightBrightness", () -> nightBrightness, v -> nightBrightness = v),
        number("emissionBrightness", () -> emissionBrightness, v -> emissionBrightness = v),
        number("heldLightBrightness", () -> heldLightBrightness, v -> heldLightBrightness = v),
        // The on/off switch of before the path settings, still written for older versions and old codes: off is
        // the tilted path. The settings after it, when present, give the path exactly.
        bool("vanillaSunPath", Options::vanillaSunPath, v -> {
            sunPathTilt = v ? 0 : SUN_PATH_TILTED_DEGREES;
            sunPathAzimuth = 0;
        }),
        number("sunPathTilt", () -> sunPathTilt, v -> sunPathTilt = clamp(v, MAX_PATH_TILT)),
        number("sunPathAzimuth", () -> sunPathAzimuth, v -> sunPathAzimuth = clamp(v, MAX_ROTATION)),
        number("sunSpriteRotation", () -> sunSpriteRotation, v -> sunSpriteRotation = clamp(v, MAX_ROTATION)),
        bool("moonFollowsSun", () -> moonFollowsSun, v -> moonFollowsSun = v),
        number("moonPathTilt", () -> moonPathTilt, v -> moonPathTilt = clamp(v, MAX_PATH_TILT)),
        number("moonPathAzimuth", () -> moonPathAzimuth, v -> moonPathAzimuth = clamp(v, MAX_ROTATION)),
        number("moonSpriteRotation", () -> moonSpriteRotation, v -> moonSpriteRotation = clamp(v, MAX_ROTATION)),
        bool("vanillaCelestialOrientation", () -> vanillaCelestialOrientation,
            v -> vanillaCelestialOrientation = v),
        bool("blockLightSampling", () -> blockLightSampling, v -> blockLightSampling = v),
        bool("rayTracingEnabled", () -> rayTracingEnabled, v -> rayTracingEnabled = v),
        new Entry("cloudsBeforeDistantHorizons", () -> cloudsBeforeDistantHorizons,
            v -> cloudsBeforeDistantHorizons = v),
        new Entry("rendererProfile", () -> rendererProfile, v -> rendererProfile = v),
        new Entry("addonsOpen", () -> addonsOpen, v -> addonsOpen = v),
        bool("useOpenGl", () -> useOpenGl, v -> useOpenGl = v),
        bool("frameGeneration", () -> frameGeneration, v -> frameGeneration = v),
        number("generatedFrames", () -> generatedFrames, v -> generatedFrames = v),
        number("frameGenerationBackend", () -> frameGenerationBackend, v -> frameGenerationBackend = v),
        bool("reflex", () -> reflex, v -> reflex = v),
        new Entry("outputResolution", () -> outputResolution, v -> setOutputResolution(v.trim())),
        number("outputResolutionPercent", () -> outputResolutionPercent,
            v -> outputResolutionPercent = com.g2806.radiante.client.render.OutputResolution.clampPercent(v)),
        bool("hdrOutput", () -> hdrOutput, v -> hdrOutput = v),
        number("hdrPeakNits", () -> hdrPeakNits, v -> hdrPeakNits = Math.max(200, Math.min(10000, v))),
        number("hdrPaperWhiteNits", () -> hdrPaperWhiteNits,
            v -> hdrPaperWhiteNits = Math.max(80, Math.min(500, v))));

    /**
     * Takes a saved output resolution. The fixed percentages of before (PERCENT_75 and the like) became the Custom
     * size with their share, so a player who chose one keeps the same picture.
     */
    private static void setOutputResolution(String name) {
        com.g2806.radiante.client.render.OutputResolution value =
            com.g2806.radiante.client.render.OutputResolution.byName(name);
        int percent = com.g2806.radiante.client.render.OutputResolution.percentOf(name);
        if (percent > 0) {
            outputResolutionPercent = percent;
        }
        outputResolution = value.name();
    }

    private static int clamp(int value, int limit) {
        return Math.max(-limit, Math.min(limit, value));
    }

    /** The saved text of the named settings, for sharing them (SettingsCode). */
    public static java.util.Map<String, String> exportValues(java.util.Collection<String> keys) {
        java.util.Map<String, String> values = new java.util.LinkedHashMap<>();
        for (Entry entry : ENTRIES) {
            if (keys.contains(entry.key())) {
                values.put(entry.key(), entry.value().get());
            }
        }
        return values;
    }

    /** Sets one setting from its saved text; the caller has checked the value and saves afterwards. */
    public static void importValue(String key, String text) {
        for (Entry entry : ENTRIES) {
            if (entry.key().equals(key)) {
                try {
                    entry.load().accept(text);
                } catch (NumberFormatException badValue) {
                    RadianteClient.LOGGER.warn("Ignoring option {}={}", key, text);
                }
                return;
            }
        }
    }

    public static void readOptions() {
        Path path = RadianteClient.radianceDir.resolve(OPTION_PROPERTIES);
        if (!Files.exists(path)) {
            applyChunkBuildingAuto();
            overwriteConfig();
            return;
        }

        Properties props = new Properties();
        try (InputStream in = Files.newInputStream(path)) {
            props.load(in);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        for (Entry entry : ENTRIES) {
            String text = props.getProperty(entry.key());
            if (text == null) {
                continue;
            }
            try {
                entry.load().accept(text);
            } catch (NumberFormatException badValue) {
                // A hand-edited or damaged value keeps its default rather than stopping the game from starting.
                RadianteClient.LOGGER.warn("Ignoring option {}={}", entry.key(), text);
            }
        }
        // A config from before the automatic setting: automatic unless the batch sliders were moved off their old 12.
        if (!props.containsKey("chunkBuildingAuto")) {
            chunkBuildingAuto = chunkBuildingBatchSize == 12 && chunkBuildingTotalBatches == 12;
        }
        applyChunkBuildingAuto();
        // Held lights used to follow the block setting; a config from before keeps them where they were.
        if (!props.containsKey("heldLightBrightness")) {
            heldLightBrightness = emissionBrightness;
        }
        overwriteConfig();
    }

    public static void overwriteConfig() {
        Path path = RadianteClient.radianceDir.resolve(OPTION_PROPERTIES);
        Properties props = new Properties();
        for (Entry entry : ENTRIES) {
            props.setProperty(entry.key(), entry.value().get());
        }

        try {
            Files.createDirectories(path.getParent());
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        try (OutputStream out = Files.newOutputStream(path)) {
            props.store(out, "Options");
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    public native static void nativeSetChunkBuildingBatchSize(int chunkBuildingBatchSize,
        boolean write);

    public static void setChunkBuildingBatchSize(int chunkBuildingBatchSize, boolean write) {
        Options.chunkBuildingBatchSize = chunkBuildingBatchSize;
        nativeSetChunkBuildingBatchSize(chunkBuildingBatchSize, write);
        if (write) {
            overwriteConfig();
        }
    }

    public native static void nativeSetChunkBuildingTotalBatches(int chunkBuildingTotalBatches,
        boolean write);

    public static void setChunkBuildingTotalBatches(int chunkBuildingTotalBatches, boolean write) {
        Options.chunkBuildingTotalBatches = chunkBuildingTotalBatches;
        nativeSetChunkBuildingTotalBatches(chunkBuildingTotalBatches, write);
        if (write) {
            overwriteConfig();
        }
    }

    public static void setChunkBuildingThreads(int chunkBuildingThreads, boolean write) {
        Options.chunkBuildingThreads = clampChunkBuildingThreads(chunkBuildingThreads);
        if (write) {
            overwriteConfig();
        }
    }

    /** Applies the setting to the renderer too, whose own output is the noisier half of it. */
    public static void setDebugLogging(boolean enabled, boolean write) {
        Options.debugLogging = enabled;
        try {
            com.g2806.radiante.client.proxy.vulkan.RendererProxy.setLoggingEnabled(enabled);
        } catch (UnsatisfiedLinkError | NoClassDefFoundError e) {
            // The native renderer is not loaded yet; it reads the flag when it is.
        }
        if (write) {
            overwriteConfig();
        }
    }

    public native static void nativeSetEntityLightReach(int reach, boolean write);

    public static void setEntityLightReach(int reach, boolean write) {
        Options.entityLightReach = Math.max(8, Math.min(256, reach));
        try {
            nativeSetEntityLightReach(Options.entityLightReach, write);
        } catch (UnsatisfiedLinkError ignored) {
            // No renderer yet; applied when it loads.
        }
    }

    public native static void nativeSetCollectChunkEmission(boolean collectChunkEmission,
        boolean write);

    public static void setCollectChunkEmission(boolean collectChunkEmission, boolean write) {
        boolean changed = Options.collectChunkEmission != collectChunkEmission;
        Options.collectChunkEmission = collectChunkEmission;
        nativeSetCollectChunkEmission(collectChunkEmission, write);

        if (changed) {
            if (collectChunkEmission) {
                TextureProxy.flushEmissionTiles();
                ChunkProxy.rebuildAll();
            } else if (write) {
                Pipeline.ensureSelectedShaderPackAvailable();
            }
        }

        if (write) {
            overwriteConfig();
        }
    }
}
