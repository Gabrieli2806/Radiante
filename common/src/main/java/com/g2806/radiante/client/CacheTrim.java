package com.g2806.radiante.client;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.nio.file.attribute.FileTime;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.stream.Stream;

/**
 * Keeps {@code radiante/cache} from growing without end. Every shader permutation the player ever used stays in the
 * SPIR-V cache, and the driver's pipeline cache only grows, so both are trimmed once at startup, before the renderer
 * opens them.
 *
 * <p>The SPIR-V cache drops its least recently used files (a cache hit touches the file) until it fits its limit. The
 * pipeline cache is one file owned by the driver, so past its limit it is deleted whole: the next start rebuilds it
 * behind the title screen warm-up, as after an update.
 */
final class CacheTrim {

    static final long SHADER_CACHE_LIMIT = 100L * 1024 * 1024;
    static final long PIPELINE_CACHE_LIMIT = 256L * 1024 * 1024;

    private CacheTrim() {
    }

    static void run(Path radianceDir) {
        Path cache = radianceDir.resolve("cache");
        try {
            trimShaders(cache.resolve("shaders"), SHADER_CACHE_LIMIT);
            trimPipelineCache(cache.resolve("pipeline_cache.bin"), PIPELINE_CACHE_LIMIT);
        } catch (IOException | RuntimeException e) {
            // A cache left as it is costs disk space, not a start.
            RadianteClient.LOGGER.warn("Radiante could not trim its caches", e);
        }
    }

    private record Entry(Path path, long size, FileTime lastUsed) {
    }

    /** Deletes the least recently used files in {@code folder} until the rest fit {@code limit} bytes. */
    static void trimShaders(Path folder, long limit) throws IOException {
        if (!Files.isDirectory(folder)) {
            return;
        }
        List<Entry> entries = new ArrayList<>();
        long total = 0;
        try (Stream<Path> files = Files.walk(folder)) {
            for (Path path : (Iterable<Path>) files::iterator) {
                BasicFileAttributes attributes = Files.readAttributes(path, BasicFileAttributes.class);
                if (attributes.isRegularFile()) {
                    entries.add(new Entry(path, attributes.size(), attributes.lastModifiedTime()));
                    total += attributes.size();
                }
            }
        }
        if (total <= limit) {
            return;
        }
        long before = total;
        int deleted = 0;
        entries.sort(Comparator.comparing(Entry::lastUsed));
        for (Entry entry : entries) {
            if (total <= limit) {
                break;
            }
            if (Files.deleteIfExists(entry.path())) {
                total -= entry.size();
                deleted++;
            }
        }
        RadianteClient.LOGGER.info("Radiante trimmed the shader cache from {} MB to {} MB ({} files deleted)",
            before >> 20, total >> 20, deleted);
    }

    /** Deletes the pipeline cache when it is larger than {@code limit} bytes. */
    static void trimPipelineCache(Path file, long limit) throws IOException {
        if (Files.isRegularFile(file) && Files.size(file) > limit) {
            long size = Files.size(file);
            Files.delete(file);
            RadianteClient.LOGGER.info("Radiante deleted the pipeline cache ({} MB, over {} MB); it is rebuilt on the "
                + "next start", size >> 20, limit >> 20);
        }
    }
}
