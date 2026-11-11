package com.g2806.radiante.client.option;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/**
 * Writing and rescuing the files a player's settings live in ({@code options.properties}, {@code pipeline.yaml}, a
 * shader pack's settings, the saved presets).
 *
 * <p>A settings file is written whole to a file beside it and then moved over the old one, so a crash or a full disk
 * in the middle of a save leaves the old file, never half of the new one. A file that cannot be read is copied aside
 * before the defaults replace it, so what the player had can still be recovered by hand.
 */
public final class SettingsFile {

    private SettingsFile() {
    }

    public static void write(Path path, String text) throws IOException {
        write(path, text.getBytes(StandardCharsets.UTF_8));
    }

    public static void write(Path path, byte[] bytes) throws IOException {
        Path folder = path.toAbsolutePath().getParent();
        Files.createDirectories(folder);
        Path temp = folder.resolve(path.getFileName() + ".tmp");
        try {
            Files.write(temp, bytes);
            try {
                Files.move(temp, path, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
            } catch (AtomicMoveNotSupportedException e) {
                // A folder on a file system without atomic moves (some network shares): the plain move is still
                // one step on the whole file.
                Files.move(temp, path, StandardCopyOption.REPLACE_EXISTING);
            }
        } finally {
            Files.deleteIfExists(temp);
        }
    }

    /**
     * Copies a file that could not be read to {@code <name>.bak} next to it and returns that copy, or null when it
     * could not be made. Never throws: this runs while a failure is already being handled.
     */
    public static Path keepDamaged(Path path) {
        try {
            if (!Files.isRegularFile(path)) {
                return null;
            }
            Path backup = path.resolveSibling(path.getFileName() + ".bak");
            Files.copy(path, backup, StandardCopyOption.REPLACE_EXISTING);
            return backup;
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }
}
