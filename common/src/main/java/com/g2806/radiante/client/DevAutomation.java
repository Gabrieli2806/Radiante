package com.g2806.radiante.client;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Screenshot;

/**
 * Scripted test runs for development, driven entirely by the {@code RADIANTE_DEV_SCRIPT} environment variable and
 * inert without it. The script is a {@code ;}-separated list of {@code tick:action} steps counted from the moment
 * the player is in a world, where the action is {@code shot=name} (screenshot to {@code screenshots/name.png}),
 * {@code cmd=/command} (run a chat command), {@code break=x,y,z,stage} (show block cracks), {@code packs=a,b} or {@code packs=none} (set the
 * resource packs and reload), {@code reload} (reload resource packs), {@code fps} (log
 * the frame rate), {@code warmup} (start the title screen's shader warm-up now, whatever the stored fingerprint says) or {@code quit}.
 */
public final class DevAutomation {

    private record Step(int tick, String action) {
    }

    private static final List<Step> STEPS = new ArrayList<>();
    /** Steps counted from the client's first tick, for everything that has to happen before a world is open. */
    private static final List<Step> PRE_STEPS = new ArrayList<>();
    private static int clientTicks = -1;
    private static int nextPre;
    private static int worldTicks = -1;
    private static int next;
    private static boolean active;
    /** A test world is being opened: its experimental-settings backup prompt is answered here, not by a person. */
    private static boolean openingTestWorld;
    private static net.minecraft.client.gui.screens.Screen answeredBackup;
    private static final String TEST_WORLD_PREFIX = "Radiante";

    /**
     * A camera glide ({@code glide=} step): so many frames left, the step each frame takes (yaw and pitch in degrees,
     * then x, y, z in blocks), and the screenshot taken once it has stopped.
     */
    private static int glideFrames;
    private static final double[] GLIDE_STEP = new double[5];
    private static String glideShot;

    private DevAutomation() {
    }

    /**
     * Moves the camera one step of a glide, once a frame before the level is drawn. Steps per frame rather than per
     * tick give the smooth motion a denoiser's history sees from a mouse (a tick's step stands still for several
     * frames and then jumps) and end at a pose known in advance, so the frame drawn as the glide ends (frameDrawn) can
     * be compared with a still, converged one from there. That frame shows the pose one step short of the last: the
     * camera of a frame is placed before this runs.
     */
    public static void frame() {
        Minecraft minecraft = Minecraft.getInstance();
        if (!active || minecraft.player == null || glideFrames == 0) {
            return;
        }
        var player = minecraft.player;
        float yaw = player.getYRot() + (float) GLIDE_STEP[0];
        float pitch = net.minecraft.util.Mth.clamp(player.getXRot() + (float) GLIDE_STEP[1], -90.0F, 90.0F);
        player.setYRot(yaw);
        player.setXRot(pitch);
        player.yRotO = yaw;
        player.xRotO = pitch;
        double x = player.getX() + GLIDE_STEP[2];
        double y = player.getY() + GLIDE_STEP[3];
        double z = player.getZ() + GLIDE_STEP[4];
        player.setPos(x, y, z);
        player.xo = x;
        player.yo = y;
        player.zo = z;
        player.xOld = x;
        player.yOld = y;
        player.zOld = z;
        glideFrames--;
    }

    /** Takes a glide's screenshot once the frame drawn at its last step is in the target. */
    public static void frameDrawn() {
        if (!active || glideFrames != 0 || glideShot == null) {
            return;
        }
        Minecraft minecraft = Minecraft.getInstance();
        String name = glideShot + ".png";
        glideShot = null;
        Screenshot.grab(minecraft.gameDirectory, name, minecraft.gameRenderer.mainRenderTarget(), 1,
            message -> RadianteClient.LOGGER.info("[dev] screenshot {}", name));
    }

    public static void register() {
        String preScript = System.getenv("RADIANTE_DEV_PRESCRIPT");
        if (preScript != null && !preScript.isBlank()) {
            for (String part : preScript.split(";")) {
                int colon = part.indexOf(':');
                if (colon > 0) {
                    PRE_STEPS.add(new Step(Integer.parseInt(part.substring(0, colon).trim()),
                        part.substring(colon + 1)));
                }
            }
            PRE_STEPS.sort((a, b) -> Integer.compare(a.tick(), b.tick()));
        }

        String script = System.getenv("RADIANTE_DEV_SCRIPT");
        if (script == null || script.isBlank()) {
            active = !PRE_STEPS.isEmpty();
            return;
        }
        for (String part : script.split(";")) {
            int colon = part.indexOf(':');
            if (colon > 0) {
                STEPS.add(new Step(Integer.parseInt(part.substring(0, colon).trim()), part.substring(colon + 1)));
            }
        }
        STEPS.sort((a, b) -> Integer.compare(a.tick(), b.tick()));
        active = true;
        RadianteClient.LOGGER.info("[dev] script with {} steps", STEPS.size());
    }

    /** Opens a test world, creating it as a flat creative world first if needed; only names with the test prefix. */
    private static void openOrCreateTestWorld(Minecraft minecraft, String name) {
        if (!name.startsWith(TEST_WORLD_PREFIX)) {
            RadianteClient.LOGGER.warn("[dev] refusing test world {}: name must start with {}", name, TEST_WORLD_PREFIX);
            return;
        }
        openingTestWorld = true;
        if (minecraft.getLevelSource().levelExists(name)) {
            minecraft.createWorldOpenFlows().openWorld(name,
                () -> RadianteClient.LOGGER.info("[dev] opening test world {} failed", name));
            RadianteClient.LOGGER.info("[dev] open test world {}", name);
            return;
        }
        net.minecraft.world.level.LevelSettings settings = new net.minecraft.world.level.LevelSettings(name,
            net.minecraft.world.level.GameType.CREATIVE, net.minecraft.world.level.LevelSettings.DifficultySettings.DEFAULT,
            true, net.minecraft.world.level.WorldDataConfiguration.DEFAULT);
        minecraft.createWorldOpenFlows().createFreshLevel(name, settings,
            new net.minecraft.world.level.levelgen.WorldOptions(name.hashCode(), false, false),
            // "Terrain" in the name asks for real terrain (hills, trees, water) instead of a flat test world.
            name.contains("Terrain") ? net.minecraft.world.level.levelgen.presets.WorldPresets::createNormalWorldDimensions
                : net.minecraft.world.level.levelgen.presets.WorldPresets::createTestWorldDimensions,
            new net.minecraft.client.gui.screens.TitleScreen());
        RadianteClient.LOGGER.info("[dev] create test world {}", name);
    }

    private static void skipBackup(net.minecraft.client.gui.screens.BackupConfirmScreen screen) {
        try {
            java.lang.reflect.Field field =
                net.minecraft.client.gui.screens.BackupConfirmScreen.class.getDeclaredField("onProceed");
            field.setAccessible(true);
            ((net.minecraft.client.gui.screens.BackupConfirmScreen.Listener) field.get(screen)).proceed(false, false);
            RadianteClient.LOGGER.info("[dev] test world backup prompt skipped");
        } catch (ReflectiveOperationException e) {
            RadianteClient.LOGGER.warn("[dev] could not answer the backup prompt", e);
            openingTestWorld = false;
        }
    }

    private static boolean isTestWorld(Minecraft minecraft) {
        var server = minecraft.getSingleplayerServer();
        if (server == null) {
            return false;
        }
        // The save folder counts too: a copy of a player's world made for testing keeps the world's own name.
        String folder = server.getWorldPath(net.minecraft.world.level.storage.LevelResource.ROOT).normalize()
            .getFileName().toString();
        return server.getWorldData().getLevelName().startsWith(TEST_WORLD_PREFIX) || folder.startsWith(TEST_WORLD_PREFIX);
    }

    /** Called every client tick by RadianteClient; does nothing unless a script was given. */
    static void tick(Minecraft minecraft) {
        if (!active) {
            return;
        }
        lastTickNanos = System.nanoTime();
        if (stallWatcher == null) {
            startStallWatcher(Thread.currentThread());
        }
        clientTicks++;
        while (nextPre < PRE_STEPS.size() && PRE_STEPS.get(nextPre).tick() <= clientTicks) {
            run(minecraft, PRE_STEPS.get(nextPre++).action());
        }

        if (openingTestWorld
            && minecraft.gui.screen() instanceof net.minecraft.client.gui.screens.BackupConfirmScreen backup) {
            // Test worlds are flat worlds with experimental settings, so opening one asks for a backup first. The
            // answer is carried out a few ticks later and the prompt stays up meanwhile; answering it twice opened the
            // world twice, and the second server found the world's lock already taken.
            if (backup != answeredBackup) {
                answeredBackup = backup;
                skipBackup(backup);
            }
        }
        if (minecraft.player == null || minecraft.level == null) {
            return;
        }
        openingTestWorld = false;
        if (worldTicks < 0 && !isTestWorld(minecraft)) {
            // Scripts teleport, fill and summon; run in a player's own world they would damage it.
            RadianteClient.LOGGER.warn("[dev] world is not a test world (name must start with {}); script stopped",
                TEST_WORLD_PREFIX);
            active = false;
            return;
        }
        worldTicks++;
        while (next < STEPS.size() && STEPS.get(next).tick() <= worldTicks) {
            run(minecraft, STEPS.get(next++).action());
        }
    }

    private static void run(Minecraft minecraft, String action) {
        if (action.equals("f3")) {
            minecraft.debugEntries.setOverlayVisible(!minecraft.debugEntries.isOverlayVisible());
            return;
        }
        if (action.equals("screen=unsupported")) {
            minecraft.gui.setScreen(new com.g2806.radiante.client.gui.UnsupportedHardwareScreen(minecraft.gui.screen()));
            return;
        }
        if (action.equals("screen=creative")) {
            // The creative inventory: many item icons at once, for checking how the GUI draws items.
            minecraft.gui.setScreen(new net.minecraft.client.gui.screens.inventory.CreativeModeInventoryScreen(
                minecraft.player, minecraft.level.enabledFeatures(), false));
            return;
        }
        if (action.equals("screen=packs")) {
            minecraft.gui.setScreen(new net.minecraft.client.gui.screens.packs.PackSelectionScreen(
                minecraft.getResourcePackRepository(), repository -> { }, minecraft.getResourcePackDirectory(),
                net.minecraft.network.chat.Component.translatable("resourcePack.title")));
            return;
        }
        if (action.startsWith("mouse=")) {
            // mouse=x,y: parks the cursor at window pixels, e.g. in a corner so no tooltip covers a screenshot.
            String[] xy = action.substring(6).split(",");
            try {
                for (String name : new String[] {"xpos", "ypos"}) {
                    java.lang.reflect.Field field = minecraft.mouseHandler.getClass().getDeclaredField(name);
                    field.setAccessible(true);
                    field.setDouble(minecraft.mouseHandler, Double.parseDouble(xy[name.equals("xpos") ? 0 : 1]));
                }
            } catch (ReflectiveOperationException e) {
                RadianteClient.LOGGER.warn("[dev] could not move the cursor", e);
            }
            return;
        }
        if (action.startsWith("litematic=")) {
            // litematic=x1,y1,z1,x2,y2,z2,ox,oy,oz: copies a box of the world into a Litematica schematic and
            // places it at another origin, for testing the schematic ghosts.
            String[] v = action.substring(10).split(",");
            int[] n = new int[9];
            for (int i = 0; i < 9; i++) {
                n[i] = Integer.parseInt(v[i]);
            }
            try {
                ClassLoader loader = DevAutomation.class.getClassLoader();
                Class<?> boxType = Class.forName("fi.dy.masa.litematica.selection.Box", true, loader);
                Class<?> areaType = Class.forName("fi.dy.masa.litematica.selection.AreaSelection", true, loader);
                Class<?> schematicType = Class.forName("fi.dy.masa.litematica.schematic.LitematicaSchematic", true, loader);
                Class<?> infoType = Class.forName(
                    "fi.dy.masa.litematica.schematic.LitematicaSchematic$SchematicSaveInfo", true, loader);
                Class<?> consumerType = Class.forName("fi.dy.masa.malilib.interfaces.IStringConsumer", true, loader);
                Class<?> placementType = Class.forName(
                    "fi.dy.masa.litematica.schematic.placement.SchematicPlacement", true, loader);
                Object box = boxType.getConstructor(net.minecraft.core.BlockPos.class, net.minecraft.core.BlockPos.class,
                    String.class).newInstance(new net.minecraft.core.BlockPos(n[0], n[1], n[2]),
                    new net.minecraft.core.BlockPos(n[3], n[4], n[5]), "main");
                Object area = areaType.getConstructor().newInstance();
                areaType.getMethod("addSubRegionBox", boxType, boolean.class).invoke(area, box, true);
                Object feedback = java.lang.reflect.Proxy.newProxyInstance(loader, new Class<?>[] {consumerType},
                    (proxy, method, arguments) -> null);
                Object schematic = schematicType.getMethod("createFromWorld", net.minecraft.world.level.Level.class,
                    areaType, infoType, String.class, consumerType).invoke(null, minecraft.level, area,
                    infoType.getConstructor(boolean.class, boolean.class).newInstance(false, false), "radiante",
                    feedback);
                Object placement = placementType.getMethod("createFor", schematicType,
                    net.minecraft.core.BlockPos.class, String.class, boolean.class, boolean.class).invoke(null,
                    schematic, new net.minecraft.core.BlockPos(n[6], n[7], n[8]), "radiante test", true, true);
                Object manager = Class.forName("fi.dy.masa.litematica.data.DataManager", true, loader)
                    .getMethod("getSchematicPlacementManager").invoke(null);
                manager.getClass().getMethod("addSchematicPlacement", placementType, boolean.class)
                    .invoke(manager, placement, false);
                RadianteClient.LOGGER.info("[dev] litematica placement at {},{},{}", n[6], n[7], n[8]);
            } catch (ReflectiveOperationException | RuntimeException e) {
                RadianteClient.LOGGER.warn("[dev] litematica placement failed", e);
            }
            return;
        }
        if (action.startsWith("jmwp=")) {
            // jmwp=x,y,z,name: a JourneyMap waypoint through its API, for testing the waypoint beams.
            String[] v = action.substring(5).split(",");
            try {
                Object waypoint = Class.forName("journeymap.api.v2.common.waypoint.WaypointFactory")
                    .getMethod("createWaypoint", String.class, net.minecraft.core.BlockPos.class, String.class,
                        net.minecraft.resources.ResourceKey.class, boolean.class)
                    .invoke(null, "radiante", new net.minecraft.core.BlockPos(Integer.parseInt(v[0]),
                        Integer.parseInt(v[1]), Integer.parseInt(v[2])), v[3], minecraft.level.dimension(), false);
                Class<?> api = Class.forName("journeymap.api.client.impl.ClientAPI");
                api.getMethod("addWaypoint", String.class,
                    Class.forName("journeymap.api.v2.common.waypoint.Waypoint"))
                    .invoke(api.getField("INSTANCE").get(null), "radiante", waypoint);
                RadianteClient.LOGGER.info("[dev] journeymap waypoint {}", v[3]);
            } catch (ReflectiveOperationException | RuntimeException e) {
                RadianteClient.LOGGER.warn("[dev] journeymap waypoint failed", e);
            }
            return;
        }
        if (action.equals("settings")) {
            minecraft.gui.setScreen(new com.g2806.radiante.client.gui.RadianteOptionsScreen(minecraft.gui.screen(), minecraft.options));
            return;
        }
        if (action.startsWith("settings=")) {
            minecraft.gui.setScreen(com.g2806.radiante.client.gui.RadianteOptionsScreen.showingPreset(minecraft.gui.screen(),
                minecraft.options, com.g2806.radiante.client.pipeline.Presets.valueOf(action.substring(9))));
            return;
        }
        if (action.startsWith("delete=")) {
            com.g2806.radiante.client.download.UpscalerDownloads.delete(
                com.g2806.radiante.client.download.UpscalerDownloads.Component.valueOf(
                    action.substring(7).toUpperCase(java.util.Locale.ROOT)));
            return;
        }
        if (action.startsWith("download=")) {
            // download=dlss / download=xess: what the download button does, for testing the downloader.
            var component = com.g2806.radiante.client.download.UpscalerDownloads.Component.valueOf(
                action.substring(9).toUpperCase(java.util.Locale.ROOT));
            com.g2806.radiante.client.download.UpscalerDownloads.start(component);
            minecraft.gui.setScreen(new com.g2806.radiante.client.gui.UpscalerDownloadScreen(minecraft.gui.screen(),
                component, com.g2806.radiante.client.gui.UpscalerDownloadScreen.Mode.CONFIRM));
            RadianteClient.LOGGER.info("[dev] download {} started", component.id);
            return;
        }
        if (action.startsWith("shot=")) {
            String name = action.substring(5) + ".png";
            Screenshot.grab(minecraft.gameDirectory, name, minecraft.gameRenderer.mainRenderTarget(), 1,
                message -> RadianteClient.LOGGER.info("[dev] screenshot {}", name));
        } else if (action.startsWith("cmd=/")) {
            minecraft.player.connection.sendCommand(action.substring(5));
            RadianteClient.LOGGER.info("[dev] command {}", action.substring(4));
        } else if (action.startsWith("break=")) {
            String[] v = action.substring(6).split(",");
            minecraft.level.destroyBlockProgress(-4242, new net.minecraft.core.BlockPos(Integer.parseInt(v[0]),
                Integer.parseInt(v[1]), Integer.parseInt(v[2])), Integer.parseInt(v[3]));
        } else if (action.startsWith("fps=")) {
            // Frames drawn in the last second, for comparing settings from a fixed camera.
            RadianteClient.LOGGER.info("[dev] fps {} {} presented {}", action.substring(4), minecraft.getFps(),
                com.g2806.radiante.client.render.FrameGeneration.presentedFrameRate());
        } else if (action.startsWith("rainmv=")) {
            com.g2806.radiante.client.render.EntityManager.rainMotion = Boolean.parseBoolean(action.substring(7));
        } else if (action.startsWith("hold=") || action.startsWith("release=")) {
            // hold=forward / release=forward (also sprint, jump): walk in a test world to see bobbing and motion.
            boolean down = action.startsWith("hold=");
            String key = action.substring(action.indexOf('=') + 1);
            net.minecraft.client.KeyMapping mapping = switch (key) {
                case "sprint" -> minecraft.options.keySprint;
                case "jump" -> minecraft.options.keyJump;
                case "back" -> minecraft.options.keyDown;
                default -> minecraft.options.keyUp;
            };
            mapping.setDown(down);
            RadianteClient.LOGGER.info("[dev] {} {}", down ? "hold" : "release", key);
        } else if (action.startsWith("glide=")) {
            // glide=frames,yaw,pitch,dx,dy,dz,shot: move the camera by that step on each of so many frames, then take
            // the screenshot (see frame()).
            String[] v = action.substring(6).split(",");
            for (int i = 0; i < GLIDE_STEP.length; i++) {
                GLIDE_STEP[i] = Double.parseDouble(v[i + 1]);
            }
            glideShot = v[6];
            glideFrames = Integer.parseInt(v[0]);
            RadianteClient.LOGGER.info("[dev] glide {}", action.substring(6));
        } else if (action.startsWith("vsync=")) {
            // vsync=false: frames as fast as they render, for measuring.
            minecraft.options.enableVsync().set(Boolean.parseBoolean(action.substring(6)));
            minecraft.options.framerateLimit().set(260);
            // A script gives no input: left at AFK, the game drops to 30 frames a second after a minute.
            minecraft.options.inactivityFpsLimit().set(net.minecraft.client.InactivityFpsLimit.MINIMIZED);
            RadianteClient.LOGGER.info("[dev] vsync {}", action.substring(6));
        } else if (action.startsWith("scroll=")) {
            // scroll=x,y,amount: the mouse wheel over the open screen at GUI coordinates (negative scrolls down).
            String[] xya = action.substring(7).split(",");
            if (minecraft.gui.screen() != null) {
                minecraft.gui.screen().mouseScrolled(Double.parseDouble(xya[0]), Double.parseDouble(xya[1]), 0.0,
                    Double.parseDouble(xya[2]));
            }
        } else if (action.equals("close")) {
            // Closes whatever screen is open (a controller mod can open the inventory in a capture run).
            minecraft.gui.setScreen(null);
        } else if (action.equals("leave")) {
            // Leaves the open screen the way Esc does, so the settings screen applies what was changed on it.
            if (minecraft.gui.screen() != null) {
                minecraft.gui.screen().onClose();
            }
        } else if (action.startsWith("press=")) {
            // Presses the first button on the open screen whose label contains the text.
            if (minecraft.gui.screen() != null) {
                pressButton(minecraft.gui.screen(), action.substring(6));
            }
        } else if (action.startsWith("click=")) {
            // A left click on the open screen at GUI coordinates, pressed and released.
            String[] xy = action.substring(6).split(",");
            if (minecraft.gui.screen() != null) {
                var event = new net.minecraft.client.input.MouseButtonEvent(Double.parseDouble(xy[0]),
                    Double.parseDouble(xy[1]), new net.minecraft.client.input.MouseButtonInfo(1, 0));
                minecraft.gui.screen().mouseClicked(event, false);
                minecraft.gui.screen().mouseReleased(event);
                RadianteClient.LOGGER.info("[dev] click {} on {}", action.substring(6),
                    minecraft.gui.screen().getClass().getSimpleName());
            }
        } else if (action.startsWith("rd=")) {
            minecraft.options.renderDistance().set(Integer.parseInt(action.substring(3)));
            RadianteClient.LOGGER.info("[dev] render distance {}", action.substring(3));
        } else if (action.startsWith("profile=")) {
            // profile=<BEDROCK_RTX_ENHANCED|CUSTOM>: applies a
            // renderer profile at once (see applyPipelineChange).
            var profile = com.g2806.radiante.client.profile.RendererProfile.of(action.substring(8));
            boolean changed = profile.apply();
            boolean rebuilt = changed && applyPipelineChange(false);
            RadianteClient.LOGGER.info("[dev] profile {} rebuilt {}{}", profile, rebuilt,
                changed && !rebuilt ? " (runtime)" : "");
        } else if (action.startsWith("pipe=")) {
            // pipe=dlss:<index> | fog:<bool> | clouds:<index> - shader pack settings, applied at once (see
            // applyPipelineChange), for benchmarks.
            String[] parts = action.substring(5).split(":", 2);
            boolean changed = switch (parts[0]) {
                case "dlss" -> com.g2806.radiante.client.pipeline.Pipeline.setDlssMode(
                    com.g2806.radiante.client.pipeline.Pipeline.DLSS_MODES.get(Integer.parseInt(parts[1])));
                case "fog" -> com.g2806.radiante.client.pipeline.Pipeline.setVolumetricFog(Boolean.parseBoolean(parts[1]));
                // preset:<preset key>, e.g. the NRD + FSR pipeline.
                case "preset" -> {
                    com.g2806.radiante.client.pipeline.Pipeline.switchToPresetMode(parts[1], false);
                    yield true;
                }
                case "clouds" -> com.g2806.radiante.client.pipeline.Pipeline.setCloudMode(
                    com.g2806.radiante.client.pipeline.Pipeline.CLOUD_MODES.get(Integer.parseInt(parts[1])));
                // attr:<attribute name>:<value>, any shader pack attribute.
                case "attr" -> {
                    String[] nameValue = parts[1].split(":", 2);
                    yield com.g2806.radiante.client.pipeline.Pipeline.setShaderPackValue(nameValue[0], nameValue[1]);
                }
                // mattr:<module name>:<attribute name>:<value>, any module's attribute.
                case "mattr" -> {
                    String[] mv = parts[1].split(":", 3);
                    yield com.g2806.radiante.client.pipeline.Pipeline.setModuleValue(mv[0], mv[1], mv[2]);
                }
                default -> false;
            };
            // A preset switch assembles a new pipeline, so it is always built.
            boolean rebuilt = changed && applyPipelineChange(parts[0].equals("preset"));
            RadianteClient.LOGGER.info("[dev] pipeline {} rebuilt {}{}", action.substring(5), rebuilt,
                changed && !rebuilt ? " (runtime)" : "");
        } else if (action.startsWith("rt=")) {
            // As the toggle key does it (RadianteClient), without touching the saved options.
            com.g2806.radiante.client.option.Options.rayTracingEnabled = Boolean.parseBoolean(action.substring(3));
            minecraft.levelExtractor.allChanged();
            RadianteClient.LOGGER.info("[dev] ray tracing {}",
                com.g2806.radiante.client.option.Options.rayTracingEnabled);
        } else if (action.startsWith("opt=")) {
            // opt=field=value: any Radiante option, for comparing looks in one run. Not saved.
            String[] parts = action.substring(4).split("=", 2);
            try {
                java.lang.reflect.Field field =
                    com.g2806.radiante.client.option.Options.class.getField(parts[0]);
                Class<?> type = field.getType();
                Object value = type == boolean.class ? Boolean.parseBoolean(parts[1])
                    : type == int.class ? Integer.parseInt(parts[1])
                    : type == float.class ? Float.parseFloat(parts[1]) : parts[1];
                field.set(null, value);
                RadianteClient.LOGGER.info("[dev] option {} = {}", parts[0], value);
            } catch (ReflectiveOperationException | RuntimeException e) {
                RadianteClient.LOGGER.warn("[dev] option {} not set", action, e);
            }
        } else if (action.startsWith("framegen=")) {
            // framegen=N: N generated frames per rendered one (0 off), as the settings screen applies it. Not saved.
            int frames = Integer.parseInt(action.substring(9));
            com.g2806.radiante.client.render.FrameGeneration.setGeneratedFrames(frames);
            RadianteClient.LOGGER.info("[dev] frame generation {} (max {})", frames,
                com.g2806.radiante.client.render.FrameGeneration.maxGeneratedFrames());
        } else if (action.startsWith("packs=")) {
            String value = action.substring(6);
            // The options list is only read at startup; the repository's selection is what a reload applies.
            var repository = minecraft.getResourcePackRepository();
            List<String> selected = new ArrayList<>();
            if (!value.equals("none")) {
                for (String pack : value.split(",")) {
                    selected.add(pack);
                }
            }
            repository.setSelected(selected);
            minecraft.options.updateResourcePacks(repository);
            RadianteClient.LOGGER.info("[dev] packs {}", value);
        } else if (action.equals("reload")) {
            minecraft.reloadResourcePacks();
            RadianteClient.LOGGER.info("[dev] reload");
        } else if (action.startsWith("rejoin=")) {
            // Leave to the title screen and open the test world again; world ticks pause meanwhile.
            minecraft.disconnectFromWorld(net.minecraft.client.multiplayer.ClientLevel.DEFAULT_QUIT_MESSAGE);
            RadianteClient.LOGGER.info("[dev] left the world");
            openOrCreateTestWorld(minecraft, action.substring(7));
        } else if (action.startsWith("testworld=")) {
            openOrCreateTestWorld(minecraft, action.substring(10));
        } else if (action.startsWith("join=")) {
            String levelId = action.substring(5);
            minecraft.createWorldOpenFlows().openWorld(levelId, () -> RadianteClient.LOGGER.info("[dev] join failed"));
            RadianteClient.LOGGER.info("[dev] join {}", levelId);
        } else if (action.startsWith("hud=")) {
            // hud=false hides the whole interface (F1), for captures that are compared pixel for pixel.
            if (minecraft.gui.hud.isHidden() == Boolean.parseBoolean(action.substring(4))) {
                minecraft.gui.hud.toggle();
            }
            RadianteClient.LOGGER.info("[dev] hud {}", action.substring(4));
        } else if (action.startsWith("fov=")) {
            minecraft.options.fov().set(Integer.parseInt(action.substring(4)));
            RadianteClient.LOGGER.info("[dev] fov {}", action.substring(4));
        } else if (action.startsWith("slot=")) {
            int slot = Integer.parseInt(action.substring(5));
            minecraft.player.getInventory().setSelectedSlot(slot);
            RadianteClient.LOGGER.info("[dev] slot {}", slot);
        } else if (action.startsWith("debug=")) {
            String value = action.substring(6);
            net.minecraft.resources.Identifier entry = switch (value) {
                case "hitboxes" -> net.minecraft.client.gui.components.debug.DebugScreenEntries.ENTITY_HITBOXES;
                case "chunkborders" -> net.minecraft.client.gui.components.debug.DebugScreenEntries.CHUNK_BORDERS;
                default -> null;
            };
            if (entry != null) {
                minecraft.debugEntries.toggleStatus(entry);
            }
            RadianteClient.LOGGER.info("[dev] debug {}", value);
        } else if (action.startsWith("camera=")) {
            String value = action.substring(7);
            minecraft.options.setCameraType(value.equals("third") ?
                    net.minecraft.client.CameraType.THIRD_PERSON_BACK :
                    value.equals("front") ? net.minecraft.client.CameraType.THIRD_PERSON_FRONT :
                                            net.minecraft.client.CameraType.FIRST_PERSON);
            RadianteClient.LOGGER.info("[dev] camera {}", value);
        } else if (action.equals("options")) {
            minecraft.gui.setScreen(new com.g2806.radiante.client.gui.RadianteOptionsScreen(null, minecraft.options));
            RadianteClient.LOGGER.info("[dev] options screen");
        } else if (action.equals("warmup")) {
            com.g2806.radiante.client.gui.PipelineWarmUp.force();
            RadianteClient.LOGGER.info("[dev] warm-up forced");
        } else if (action.equals("fps")) {
            RadianteClient.LOGGER.info("[dev] fps {} at tick {}", minecraft.getFps(), worldTicks);
        } else if (action.equals("quit")) {
            minecraft.stop();
        }
    }

    /**
     * Changed pipeline settings reach the renderer as the settings screen hands them over: built anew when something
     * compiled in changed ({@code structural}, or what Pipeline noted), otherwise pushed live, so scripts test that
     * path too. Saved either way, as a build saves. True when it was built.
     */
    private static boolean applyPipelineChange(boolean structural) {
        if (structural || com.g2806.radiante.client.pipeline.Pipeline.takeStructuralChange()
            || !com.g2806.radiante.client.pipeline.Pipeline.pushRuntimeChanges()) {
            com.g2806.radiante.client.pipeline.Pipeline.build();
            return true;
        }
        com.g2806.radiante.client.pipeline.Pipeline.savePipeline();
        return false;
    }

    private static boolean pressButton(net.minecraft.client.gui.components.events.ContainerEventHandler parent,
        String text) {
        for (net.minecraft.client.gui.components.events.GuiEventListener child : parent.children()) {
            if (child instanceof net.minecraft.client.gui.components.AbstractButton button
                && button.getMessage().getString().contains(text)) {
                button.onPress(new net.minecraft.client.input.MouseButtonInfo(0, 0));
                RadianteClient.LOGGER.info("[dev] pressed {}", button.getMessage().getString());
                return true;
            }
            if (child instanceof net.minecraft.client.gui.components.events.ContainerEventHandler container
                && pressButton(container, text)) {
                return true;
            }
        }
        return false;
    }

    private static volatile long lastTickNanos;
    private static Thread stallWatcher;

    /** Development: logs where the render thread is while it has not ticked for over a second. */
    private static void startStallWatcher(Thread renderThread) {
        stallWatcher = new Thread(() -> {
            while (true) {
                try {
                    Thread.sleep(500);
                } catch (InterruptedException e) {
                    return;
                }
                long stalledMs = (System.nanoTime() - lastTickNanos) / 1_000_000L;
                if (stalledMs < 1000) {
                    continue;
                }
                StringBuilder trace = new StringBuilder();
                StackTraceElement[] stack = renderThread.getStackTrace();
                for (int i = 0; i < Math.min(14, stack.length); i++) {
                    trace.append(" <- ").append(stack[i]);
                }
                RadianteClient.LOGGER.info("[dev] render thread stalled {} ms:{}", stalledMs, trace);
            }
        }, "Radiante dev stall watcher");
        stallWatcher.setDaemon(true);
        stallWatcher.start();
    }
}
