package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.ShaderPackLibrary;
import com.g2806.radiante.client.pipeline.ShaderPackValidation;
import com.g2806.radiante.client.render.RadianteRenderer;
import net.minecraft.client.Minecraft;
import net.minecraft.network.chat.Component;

import java.io.IOException;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.function.Consumer;

/**
 * Reloads the active shader pack when it is a folder in {@code radiante/shaderpacks/} and its files change, so a pack
 * author sees an edit in game without a restart. The folder is looked at once a second (names, sizes and times of its
 * files, without reading them); once a change has settled for a second the pack is checked as a build would load it
 * ({@link ShaderPackValidation}), on a background thread while the world keeps drawing. Only a pack that loads and
 * compiles is built, through the usual background rebuild; a broken edit leaves the running pipeline alone and says
 * what is wrong in a popup and on the Shader Packs screen, so the picture never goes black while typing.
 *
 * <p>Zipped packs are not watched: they are not edited in place. Folders whose name starts with a dot ({@code .git})
 * are skipped, and so is a pack of more than {@link #MAX_FILES} files.
 */
public final class ShaderPackHotReload {

    /** Ticks between two looks at the folder. */
    private static final int POLL_TICKS = 20;
    /** The loader's own limit (shader_pack_extends.cpp): a bigger folder is not a pack it would load. */
    private static final int MAX_FILES = 20000;

    private static String watchedPath;
    private static long fingerprint;
    /** A change seen at the last look, built once the folder looks the same again. */
    private static boolean changeSettling;
    private static int ticks;
    private static CompletableFuture<ShaderPackValidation> check;
    /** Where the result of the running check goes besides the reload: the command's chat report. */
    private static Consumer<ShaderPackValidation> listener;
    /** Whether the running check reloads the pack when it passes ({@code /radiante pack validate} does not). */
    private static boolean reloadOnSuccess;

    private ShaderPackHotReload() {
    }

    /** Every client tick. */
    public static void tick(Minecraft minecraft) {
        if (check != null) {
            if (check.isDone()) {
                finishCheck(minecraft);
            }
            return;
        }
        if (++ticks < POLL_TICKS) {
            return;
        }
        ticks = 0;
        Path folder = activeFolderPack();
        String path = folder == null ? null : Pipeline.shaderPackPath();
        if (folder == null || !RadianteRenderer.isActive()) {
            watchedPath = null;
            changeSettling = false;
            return;
        }
        long now = fingerprint(folder);
        if (!path.equals(watchedPath)) {
            // A pack just chosen is built by the switch itself; only later edits reload it.
            watchedPath = path;
            fingerprint = now;
            changeSettling = false;
            return;
        }
        if (now != fingerprint) {
            fingerprint = now;
            changeSettling = true;
            return;
        }
        if (changeSettling) {
            changeSettling = false;
            startCheck(minecraft, path, true, null);
        }
    }

    /**
     * Checks a pack now, in the background; false when a check is already running. With {@code reload}, a pack that
     * passes is built (it must be the active one). {@code report} gets the result on the client thread.
     */
    public static boolean startCheck(Minecraft minecraft, String packPath, boolean reload,
                                     Consumer<ShaderPackValidation> report) {
        if (check != null) {
            return false;
        }
        reloadOnSuccess = reload;
        listener = report;
        if (reload) {
            RebuildToast.running(minecraft, 0.0f, Component.translatable("toast.radiante.pack_reload"),
                Component.translatable("toast.radiante.pack_reload.checking", packName(packPath)));
            RadianteClient.LOGGER.info("[shader packs] {} changed; checking it before reloading", packPath);
        }
        check = ShaderPackValidation.start(packPath);
        return true;
    }

    /** Whether the active pack is a folder that is watched (and so can be reloaded). */
    public static boolean canReload() {
        return activeFolderPack() != null;
    }

    private static void finishCheck(Minecraft minecraft) {
        ShaderPackValidation result;
        try {
            result = check.join();
        } catch (RuntimeException e) {
            result = new ShaderPackValidation(watchedPath, false, List.of(String.valueOf(e.getMessage())), 0, 0);
        }
        check = null;
        Consumer<ShaderPackValidation> report = listener;
        listener = null;
        String name = packName(result.packPath());
        if (!result.ok()) {
            List<String> lines = result.reportLines();
            for (String line : lines) {
                RadianteClient.LOGGER.warn("[shader packs] {}: {}", name, line);
            }
            ShaderPackTrial.reportCheckFailure(name, result.packPath(), lines);
            if (reloadOnSuccess) {
                RebuildToast.failed(minecraft, Component.translatable("toast.radiante.pack_reload.failed"),
                    lines.isEmpty() ? null : lines.get(0));
            }
        } else if (reloadOnSuccess && ShaderPackLibrary.samePack(result.packPath(), Pipeline.shaderPackPath())) {
            RadianteClient.LOGGER.info("[shader packs] {} compiles ({} shaders, {} compiled); reloading", name,
                result.shaders(), result.compiled());
            Pipeline.requestRebuild();
            BackgroundRebuild.apply(minecraft);
        }
        if (report != null) {
            report.accept(result);
        }
        // Edits made while it was checked are picked up at the next look.
    }

    /** The active pack's folder when it is a folder pack under radiante/shaderpacks/, else null. */
    private static Path activeFolderPack() {
        String path = Pipeline.shaderPackPath();
        if (path == null || RadianteClient.radianceDir == null
            || ShaderPackLibrary.samePack(path, Pipeline.builtInShaderPackPath())) {
            return null;
        }
        Path folder = Path.of(path);
        if (!folder.isAbsolute()) {
            folder = RadianteClient.radianceDir.resolve(folder);
        }
        folder = folder.toAbsolutePath().normalize();
        Path packs = ShaderPackLibrary.folder();
        if (packs == null || !folder.startsWith(packs.toAbsolutePath().normalize()) || !Files.isDirectory(folder)) {
            return null;
        }
        return folder;
    }

    /** The pack's file or folder name, as the list shows it when it has no display name. */
    static String packName(String path) {
        if (path == null) {
            return "";
        }
        String normalized = path.replace('\\', '/');
        return normalized.substring(normalized.lastIndexOf('/') + 1);
    }

    /** Names, sizes and modification times of the folder's files, folded into one number. */
    private static long fingerprint(Path folder) {
        long[] state = {1125899906842597L, 0};
        try {
            Files.walkFileTree(folder, new SimpleFileVisitor<>() {
                @Override
                public FileVisitResult preVisitDirectory(Path dir, BasicFileAttributes attributes) {
                    Path name = dir.getFileName();
                    return !dir.equals(folder) && name != null && name.toString().startsWith(".")
                        ? FileVisitResult.SKIP_SUBTREE : FileVisitResult.CONTINUE;
                }

                @Override
                public FileVisitResult visitFile(Path file, BasicFileAttributes attributes) {
                    if (++state[1] > MAX_FILES) {
                        return FileVisitResult.TERMINATE;
                    }
                    long hash = state[0];
                    hash = 31 * hash + folder.relativize(file).toString().hashCode();
                    hash = 31 * hash + attributes.size();
                    hash = 31 * hash + attributes.lastModifiedTime().toMillis();
                    state[0] = hash;
                    return FileVisitResult.CONTINUE;
                }

                @Override
                public FileVisitResult visitFileFailed(Path file, IOException e) {
                    return FileVisitResult.CONTINUE;
                }
            });
        } catch (IOException e) {
            return state[0];
        }
        return state[0];
    }
}
