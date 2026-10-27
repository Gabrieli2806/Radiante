package com.g2806.radiante.client.bedrock;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * The files of the resource packs inside a Bedrock archive, read as one pack. A {@code .mcpack} usually holds one
 * pack; an {@code .mcaddon} holds several, side by side in folders or as {@code .mcpack} files of their own, and one of
 * them is often a behavior pack. Every folder with a {@code manifest.json} is a pack, and so is every nested
 * {@code .mcpack} (or {@code .zip}, {@code .mcaddon}) outside those folders.
 *
 * <p>Only resource packs are kept: a pack whose manifest lists modules and none of type {@code resources} (data,
 * script, ...) holds blocks, functions and JavaScript that only Bedrock's engine runs; it is listed in
 * {@link #skipped()} for the log. A manifest without modules, or one that cannot be read, still counts as a resource
 * pack, as every .mcpack did before.
 *
 * <p>A resource pack with subpacks is read as its first subpack laid over the pack itself, as Bedrock lays the
 * chosen one. The first listed is taken: Bedrock lets the player pick on its pack settings and there is no such
 * choice here, and authors list the plain variant first (the memory tiers that order the slider say how heavy a
 * subpack is, not which one is meant). Several resource packs are laid over each other in the order found, the first
 * on top.
 */
public final class BedrockPackFiles implements Closeable {

    /** How deep archives inside archives are opened: an .mcaddon holding .mcpack files is one level. */
    private static final int MAX_NESTING = 3;

    /** A pack found inside the archive, for the log: its name, where it is and, for a resource pack, its subpack. */
    public record Pack(String name, String where, String subpack, List<String> moduleTypes) {
    }

    private record Layer(ZipFile zip, String prefix) {
    }

    private final List<Layer> layers = new ArrayList<>();
    private final List<ZipFile> opened = new ArrayList<>();
    private final List<Path> extracted = new ArrayList<>();
    private final List<Pack> resourcePacks = new ArrayList<>();
    private final List<Pack> skipped = new ArrayList<>();
    private final Path scratch;

    private BedrockPackFiles(Path scratch) {
        this.scratch = scratch;
    }

    /**
     * Opens the resource packs in {@code archive}. Nested archives are copied into {@code scratch} while open and
     * deleted on {@link #close()}.
     */
    public static BedrockPackFiles open(Path archive, Path scratch) throws IOException {
        BedrockPackFiles files = new BedrockPackFiles(scratch);
        try {
            ZipFile zip = new ZipFile(archive.toFile());
            files.opened.add(zip);
            files.discover(zip, "", 0, true);
        } catch (IOException | RuntimeException e) {
            files.close();
            throw e;
        }
        return files;
    }

    /** The resource packs read, top first. */
    public List<Pack> resourcePacks() {
        return Collections.unmodifiableList(resourcePacks);
    }

    /** The behavior, script and other packs that were left out. */
    public List<Pack> skipped() {
        return Collections.unmodifiableList(skipped);
    }

    /** The name of the topmost resource pack from its manifest, without Bedrock's colour codes; null without one. */
    public String name() {
        for (Pack pack : resourcePacks) {
            if (pack.name() != null && !pack.name().isBlank()) {
                return pack.name();
            }
        }
        return null;
    }

    /** A file by its path in the pack, from the topmost layer that has it; null when none does. */
    public byte[] read(String path) throws IOException {
        for (Layer layer : layers) {
            ZipEntry entry = layer.zip().getEntry(layer.prefix() + path);
            if (entry != null && !entry.isDirectory()) {
                try (InputStream in = layer.zip().getInputStream(entry)) {
                    return in.readAllBytes();
                }
            }
        }
        return null;
    }

    /** Every file path in the pack, each once. */
    public Set<String> paths() {
        Set<String> paths = new LinkedHashSet<>();
        for (Layer layer : layers) {
            for (var entries = layer.zip().entries(); entries.hasMoreElements(); ) {
                ZipEntry entry = entries.nextElement();
                if (!entry.isDirectory() && entry.getName().startsWith(layer.prefix())) {
                    String path = entry.getName().substring(layer.prefix().length());
                    // A subpack's files are read through the subpack's own layer, not as part of the pack root.
                    if (!path.startsWith("subpacks/") || layer.prefix().contains("subpacks/")) {
                        paths.add(path);
                    }
                }
            }
        }
        return paths;
    }

    @Override
    public void close() throws IOException {
        IOException failure = null;
        for (ZipFile zip : opened) {
            try {
                zip.close();
            } catch (IOException e) {
                failure = e;
            }
        }
        opened.clear();
        layers.clear();
        for (Path path : extracted) {
            Files.deleteIfExists(path);
        }
        extracted.clear();
        if (failure != null) {
            throw failure;
        }
    }

    /**
     * Finds the packs in one archive. {@code where} names the archive inside its parents, for the log;
     * {@code outermost} is the file in the resource pack folder itself, which is one pack when nothing else is found.
     */
    private void discover(ZipFile zip, String where, int depth, boolean outermost) throws IOException {
        List<String> roots = new ArrayList<>();
        List<String> archives = new ArrayList<>();
        for (var entries = zip.entries(); entries.hasMoreElements(); ) {
            ZipEntry entry = entries.nextElement();
            String name = entry.getName();
            if (entry.isDirectory()) {
                continue;
            }
            if (name.equals("manifest.json") || name.endsWith("/manifest.json")) {
                roots.add(name.substring(0, name.length() - "manifest.json".length()));
            } else if (isArchive(name)) {
                archives.add(name);
            }
        }
        // A manifest inside another pack's folder is part of that pack (or stray), not a pack of its own; the
        // shallowest manifest is the pack, as for the .mcpack files that wrap their pack in a folder.
        List<String> byDepth = new ArrayList<>(roots);
        byDepth.sort((a, b) -> Integer.compare(a.length(), b.length()));
        Set<String> packRoots = new LinkedHashSet<>();
        for (String root : byDepth) {
            if (packRoots.stream().noneMatch(root::startsWith)) {
                packRoots.add(root);
            }
        }
        // In the archive's own order, so the pack listed first stays on top.
        List<String> ordered = new ArrayList<>();
        for (String root : roots) {
            if (packRoots.contains(root)) {
                ordered.add(root);
            }
        }
        for (String root : ordered) {
            addPack(zip, root, where);
        }
        if (depth < MAX_NESTING) {
            for (String name : archives) {
                if (ordered.stream().anyMatch(name::startsWith)) {
                    continue;
                }
                Path copy = Files.createTempFile(Files.createDirectories(scratch), "nested", ".zip");
                extracted.add(copy);
                try (InputStream in = zip.getInputStream(zip.getEntry(name))) {
                    Files.copy(in, copy, StandardCopyOption.REPLACE_EXISTING);
                }
                ZipFile nested;
                try {
                    nested = new ZipFile(copy.toFile());
                } catch (IOException e) {
                    continue;
                }
                opened.add(nested);
                discover(nested, where + name + "!/", depth + 1, false);
            }
        }
        if (outermost && layers.isEmpty() && skipped.isEmpty()) {
            // No manifest anywhere: the whole archive is the pack, as before packs inside archives were looked for.
            layers.add(new Layer(zip, ""));
            resourcePacks.add(new Pack(null, where, null, List.of()));
        }
    }

    private void addPack(ZipFile zip, String root, String where) {
        JsonObject manifest = null;
        try {
            ZipEntry entry = zip.getEntry(root + "manifest.json");
            try (InputStream in = zip.getInputStream(entry)) {
                // Bedrock tolerates comments in its JSON; Gson's lenient parser does too.
                manifest = JsonParser.parseString(new String(in.readAllBytes(), StandardCharsets.UTF_8))
                    .getAsJsonObject();
            }
        } catch (IOException | RuntimeException ignored) {
            // A manifest Bedrock itself would reject is still read as a resource pack, as before.
        }
        String name = null;
        List<String> types = new ArrayList<>();
        JsonArray subpacks = null;
        if (manifest != null) {
            JsonObject header = manifest.has("header") && manifest.get("header").isJsonObject()
                ? manifest.getAsJsonObject("header") : null;
            if (header != null && header.has("name") && header.get("name").isJsonPrimitive()) {
                name = header.get("name").getAsString().replaceAll("§.", "").trim();
            }
            if (manifest.has("modules") && manifest.get("modules").isJsonArray()) {
                for (JsonElement module : manifest.getAsJsonArray("modules")) {
                    if (module.isJsonObject() && module.getAsJsonObject().has("type")) {
                        types.add(module.getAsJsonObject().get("type").getAsString().toLowerCase(Locale.ROOT));
                    }
                }
            }
            if (manifest.has("subpacks") && manifest.get("subpacks").isJsonArray()) {
                subpacks = manifest.getAsJsonArray("subpacks");
            }
        }
        String location = where + root;
        if (!types.isEmpty() && !types.contains("resources")) {
            skipped.add(new Pack(name, location, null, List.copyOf(types)));
            return;
        }
        String subpack = subpacks == null ? null : firstSubpack(zip, root, subpacks);
        if (subpack != null) {
            layers.add(new Layer(zip, root + "subpacks/" + subpack + "/"));
        }
        layers.add(new Layer(zip, root));
        resourcePacks.add(new Pack(name, location, subpack, List.copyOf(types)));
    }

    /** The folder of the first subpack listed that the archive has files for, or null. */
    private static String firstSubpack(ZipFile zip, String root, JsonArray subpacks) {
        for (JsonElement element : subpacks) {
            if (!element.isJsonObject() || !element.getAsJsonObject().has("folder_name")) {
                continue;
            }
            String folder = element.getAsJsonObject().get("folder_name").getAsString();
            String prefix = root + "subpacks/" + folder + "/";
            for (var entries = zip.entries(); entries.hasMoreElements(); ) {
                if (entries.nextElement().getName().startsWith(prefix)) {
                    return folder;
                }
            }
        }
        return null;
    }

    private static boolean isArchive(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        return lower.endsWith(".mcpack") || lower.endsWith(".mcaddon") || lower.endsWith(".zip");
    }
}
