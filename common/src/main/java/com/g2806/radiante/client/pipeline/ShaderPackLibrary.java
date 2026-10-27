package com.g2806.radiante.client.pipeline;

import com.g2806.radiante.client.RadianteClient;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Enumeration;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * The shader packs a player can choose: the built-in Radiante PT and every pack in {@code radiante/shaderpacks/}, a
 * zip or a folder with {@code configs.json} at its top. Radiante's own folder rather than the game's
 * {@code shaderpacks/}: Iris and OptiFine read that one, and their packs are a different format that would only show
 * up here as broken entries (and ours as broken entries there).
 *
 * <p>What the list shows comes from the pack's manifest in {@code configs.json}: Radiance's {@code "radiance"}
 * object ({@code display_name}), or a {@code "radiante"} object that may also give {@code version}, {@code author},
 * {@code description}, {@code license}, {@code api_version} and {@code icon} (a PNG inside the pack). A
 * {@code pack.png} or {@code icon.png} at the top of the pack is used as the icon when the manifest names none.
 * Only metadata is read here; the renderer loads the pack itself.
 */
public final class ShaderPackLibrary {

    /** The folder, relative to Radiante's own ({@code radiante/}). */
    public static final String FOLDER = "shaderpacks";
    private static final String CONFIG = "configs.json";
    /** Icons larger than this are not read: a pack's icon is drawn at 32 pixels. */
    private static final long LARGEST_ICON = 1024 * 1024;

    /**
     * One pack. {@code path} is what the pipeline stores ({@code shaderpacks/<name>}); {@code problem} is the
     * translation key of why the pack cannot be used as it is, with {@code problemDetail} as its argument (null when
     * nothing is wrong that can be seen without loading it).
     */
    public record Entry(String path, String fileName, String displayName, String version, String author,
                        String description, String license, String apiVersion, byte[] icon, String problem,
                        String problemDetail, boolean builtIn) {
    }

    private ShaderPackLibrary() {
    }

    /** The folder packs are put in, made when missing; null before the mod's folder is known. */
    public static Path folder() {
        if (RadianteClient.radianceDir == null) {
            return null;
        }
        Path folder = RadianteClient.radianceDir.resolve(FOLDER);
        try {
            Files.createDirectories(folder);
        } catch (IOException e) {
            RadianteClient.LOGGER.warn("Could not create the shader pack folder {}", folder, e);
        }
        return folder;
    }

    /** The built-in pack first, then the folder's packs by name. Reads every pack's manifest: call it sparingly. */
    public static List<Entry> scan() {
        List<Entry> entries = new ArrayList<>();
        entries.add(builtIn());
        Path folder = folder();
        if (folder == null || !Files.isDirectory(folder)) {
            return entries;
        }
        List<Entry> found = new ArrayList<>();
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(folder)) {
            for (Path candidate : stream) {
                String name = candidate.getFileName().toString();
                boolean zip = Files.isRegularFile(candidate) && name.toLowerCase(Locale.ROOT).endsWith(".zip");
                if (!zip && !Files.isDirectory(candidate)) {
                    continue; // The packs' settings files (<pack>.txt) and anything else lying there.
                }
                found.add(read(candidate, FOLDER + "/" + name, false));
            }
        } catch (IOException e) {
            RadianteClient.LOGGER.warn("Could not list the shader pack folder {}", folder, e);
        }
        found.sort(Comparator.comparing(entry -> entry.fileName().toLowerCase(Locale.ROOT)));
        entries.addAll(found);
        return entries;
    }

    /** The built-in pack, read from where the native library installed it. */
    public static Entry builtIn() {
        String stored = Pipeline.builtInShaderPackPath();
        Path zip = RadianteClient.radianceDir == null ? null : RadianteClient.radianceDir.resolve(stored);
        if (zip != null && Files.isRegularFile(zip)) {
            Entry read = read(zip, stored, true);
            return new Entry(read.path(), read.fileName(), read.displayName(), read.version(), "Radiante",
                read.description(), read.license() != null ? read.license() : "GPL-3.0", read.apiVersion(),
                read.icon(), read.problem(), read.problemDetail(), true);
        }
        return new Entry(stored, "radiante-pt.zip", "Radiante PT", null, "Radiante", null, "GPL-3.0", null, null,
            null, null, true);
    }

    /** Whether two stored paths name the same pack (the built-in one may be stored as an empty path too). */
    public static boolean samePack(String a, String b) {
        String left = a == null || a.isBlank() ? Pipeline.builtInShaderPackPath() : a.replace('\\', '/');
        String right = b == null || b.isBlank() ? Pipeline.builtInShaderPackPath() : b.replace('\\', '/');
        return left.equals(right);
    }

    private static Entry read(Path pack, String stored, boolean builtIn) {
        String fileName = pack.getFileName().toString();
        String fallbackName = fileName.toLowerCase(Locale.ROOT).endsWith(".zip")
            ? fileName.substring(0, fileName.length() - 4) : fileName;
        try (PackFiles files = PackFiles.open(pack)) {
            byte[] config = files.read(CONFIG);
            if (config == null) {
                String nested = files.nestedConfigFolder();
                return new Entry(stored, fileName, fallbackName, null, null, null, null, null, null,
                    nested != null ? "screen.radiante.shader_packs.problem.nested"
                        : "screen.radiante.shader_packs.problem.no_config", nested, builtIn);
            }
            JsonObject root;
            try {
                JsonElement parsed = JsonParser.parseString(new String(config, StandardCharsets.UTF_8));
                if (!parsed.isJsonObject()) {
                    throw new IllegalStateException("the top of configs.json is not an object");
                }
                root = parsed.getAsJsonObject();
            } catch (RuntimeException e) {
                return new Entry(stored, fileName, fallbackName, null, null, null, null, null, null,
                    "screen.radiante.shader_packs.problem.bad_config", e.getMessage(), builtIn);
            }
            JsonObject manifest = object(root, "radiante");
            if (manifest == null) {
                manifest = object(root, "radiance");
            }
            String displayName = text(manifest, "display_name");
            String iconPath = text(manifest, "icon");
            byte[] icon = files.readSmall(iconPath != null ? iconPath : "pack.png");
            if (icon == null && iconPath == null) {
                icon = files.readSmall("icon.png");
            }
            String problem = root.has("passes") ? null : "screen.radiante.shader_packs.problem.no_passes";
            return new Entry(stored, fileName, displayName != null && !displayName.isBlank() ? displayName : fallbackName,
                text(manifest, "version"), author(manifest), text(manifest, "description"), text(manifest, "license"),
                text(manifest, "api_version"), icon, problem, null, builtIn);
        } catch (IOException | RuntimeException e) {
            return new Entry(stored, fileName, fallbackName, null, null, null, null, null, null,
                "screen.radiante.shader_packs.problem.unreadable", e.getMessage(), builtIn);
        }
    }

    private static JsonObject object(JsonObject root, String key) {
        JsonElement value = root == null ? null : root.get(key);
        return value != null && value.isJsonObject() ? value.getAsJsonObject() : null;
    }

    private static String text(JsonObject object, String key) {
        JsonElement value = object == null ? null : object.get(key);
        if (value == null || value.isJsonNull()) {
            return null;
        }
        return value.isJsonPrimitive() ? value.getAsString() : null;
    }

    /** {@code author} as text, or {@code authors} as a list joined with commas. */
    private static String author(JsonObject manifest) {
        String author = text(manifest, "author");
        if (author != null || manifest == null || !manifest.has("authors") || !manifest.get("authors").isJsonArray()) {
            return author;
        }
        List<String> names = new ArrayList<>();
        for (JsonElement name : manifest.getAsJsonArray("authors")) {
            if (name.isJsonPrimitive()) {
                names.add(name.getAsString());
            }
        }
        return names.isEmpty() ? null : String.join(", ", names);
    }

    /** The files of a pack, zipped or in a folder, read without unpacking anything. */
    private static final class PackFiles implements AutoCloseable {
        private final Path folder;
        private final ZipFile zipFile;

        private PackFiles(Path folder, ZipFile zipFile) {
            this.folder = folder;
            this.zipFile = zipFile;
        }

        static PackFiles open(Path pack) throws IOException {
            return Files.isDirectory(pack) ? new PackFiles(pack, null) : new PackFiles(null, new ZipFile(pack.toFile()));
        }

        byte[] read(String name) throws IOException {
            return read(name, Long.MAX_VALUE);
        }

        /** A file of at most {@link #LARGEST_ICON} bytes, or null; never throws for a missing or odd one. */
        byte[] readSmall(String name) {
            try {
                return read(name, LARGEST_ICON);
            } catch (IOException | RuntimeException e) {
                return null;
            }
        }

        private byte[] read(String name, long largest) throws IOException {
            if (name == null || name.isBlank() || name.contains("..")) {
                return null;
            }
            if (this.zipFile != null) {
                ZipEntry entry = this.zipFile.getEntry(name);
                if (entry == null || entry.isDirectory() || entry.getSize() > largest) {
                    return null;
                }
                try (InputStream in = this.zipFile.getInputStream(entry)) {
                    return in.readAllBytes();
                }
            }
            Path file = this.folder.resolve(name).normalize();
            if (!file.startsWith(this.folder) || !Files.isRegularFile(file) || Files.size(file) > largest) {
                return null;
            }
            return Files.readAllBytes(file);
        }

        /** The one folder a misplaced configs.json sits in ("Pack-main/"), as GitHub's zips wrap a repository. */
        String nestedConfigFolder() throws IOException {
            if (this.zipFile != null) {
                Enumeration<? extends ZipEntry> entries = this.zipFile.entries();
                while (entries.hasMoreElements()) {
                    String name = entries.nextElement().getName();
                    int slash = name.indexOf('/');
                    if (slash > 0 && name.equals(name.substring(0, slash + 1) + CONFIG)) {
                        return name.substring(0, slash);
                    }
                }
                return null;
            }
            try (DirectoryStream<Path> stream = Files.newDirectoryStream(this.folder)) {
                for (Path child : stream) {
                    if (Files.isDirectory(child) && Files.isRegularFile(child.resolve(CONFIG))) {
                        return child.getFileName().toString();
                    }
                }
            }
            return null;
        }

        @Override
        public void close() throws IOException {
            if (this.zipFile != null) {
                this.zipFile.close();
            }
        }
    }
}
