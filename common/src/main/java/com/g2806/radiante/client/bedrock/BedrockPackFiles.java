package com.g2806.radiante.client.bedrock;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
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
 * {@code .mcpack} or {@code .mcaddon} outside those folders.
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
 *
 * <p>Packs are downloaded files, so nothing in them is trusted: nested archives are copied out only up to fixed
 * limits (count, size, compression ratio), under names of Radiante's own choosing in the scratch folder (an entry's
 * name never becomes a path), and no single file is read past {@link #MAX_ENTRY_BYTES}. What a limit stops is listed
 * in {@link #warnings()}.
 */
public final class BedrockPackFiles implements Closeable {

    /** How deep archives inside archives are opened: an .mcaddon holding .mcpack files is one level. */
    private static final int MAX_NESTING = 2;
    /** Nested archives opened in all, and inside any one archive. */
    private static final int MAX_NESTED_ARCHIVES = 16;
    private static final int MAX_NESTED_PER_ARCHIVE = 8;
    /** Bytes one nested archive may unpack to, and all of them together. */
    private static final long MAX_NESTED_BYTES = 256L * 1024 * 1024;
    private static final long MAX_TOTAL_NESTED_BYTES = 512L * 1024 * 1024;
    /**
     * Largest unpacked-to-packed ratio of a nested archive's entry. Packs of textures compress a few times at most;
     * a zip bomb compresses a thousand times over.
     */
    private static final long MAX_COMPRESSION_RATIO = 100;
    /** Largest file read from a pack: a 2048 px TGA with alpha is 16 MB, so this leaves room. */
    static final long MAX_ENTRY_BYTES = 64L * 1024 * 1024;
    /** Packs looked at in one file. */
    private static final int MAX_PACKS = 32;

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
    private final List<String> warnings = new ArrayList<>();
    private final Path scratch;
    private int nestedArchives;
    private long nestedBytes;

    private BedrockPackFiles(Path scratch) {
        this.scratch = scratch.toAbsolutePath().normalize();
    }

    /**
     * Opens the resource packs in {@code archive}. Nested archives are copied into {@code scratch} while open and
     * deleted on {@link #close()} (or at once, when they turn out to hold no resource pack).
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

    /** What a safety limit left out (a nested archive too large, too many of them, ...), for the log. */
    public List<String> warnings() {
        return Collections.unmodifiableList(warnings);
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

    /**
     * A file by its path in the pack, from the topmost layer that has it; null when none does. A file past
     * {@link #MAX_ENTRY_BYTES} is refused with an IOException, whatever size its entry claims.
     */
    public byte[] read(String path) throws IOException {
        for (Layer layer : layers) {
            ZipEntry entry = layer.zip().getEntry(layer.prefix() + path);
            if (entry != null && !entry.isDirectory()) {
                if (entry.getSize() > MAX_ENTRY_BYTES) {
                    throw new IOException(path + " is larger than " + MAX_ENTRY_BYTES + " bytes");
                }
                try (InputStream in = layer.zip().getInputStream(entry)) {
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    copyBounded(in, out, MAX_ENTRY_BYTES, path);
                    return out.toByteArray();
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
            try {
                Files.deleteIfExists(path);
            } catch (IOException e) {
                failure = e;
            }
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
            if (resourcePacks.size() + skipped.size() >= MAX_PACKS) {
                warnings.add("more than " + MAX_PACKS + " packs in " + describe(where) + ", the rest are left out");
                break;
            }
            addPack(zip, root, where);
        }
        // Archives inside a pack's own folder are that pack's files (or junk), never followed: only an .mcpack or
        // .mcaddon lying in the container itself, as Bedrock's importer reads them.
        List<String> nestedHere = new ArrayList<>();
        for (String name : archives) {
            if (ordered.stream().noneMatch(name::startsWith)) {
                nestedHere.add(name);
            }
        }
        if (!nestedHere.isEmpty() && depth >= MAX_NESTING) {
            warnings.add("archives nested deeper than " + MAX_NESTING + " levels in " + describe(where)
                + " are left out");
            nestedHere.clear();
        }
        int here = 0;
        for (String name : nestedHere) {
            if (here >= MAX_NESTED_PER_ARCHIVE || nestedArchives >= MAX_NESTED_ARCHIVES) {
                warnings.add("too many nested archives in " + describe(where) + " (at most " + MAX_NESTED_PER_ARCHIVE
                    + " per archive and " + MAX_NESTED_ARCHIVES + " in all), the rest are left out");
                break;
            }
            here++;
            nestedArchives++;
            openNested(zip, zip.getEntry(name), where + name + "!/", depth + 1);
        }
        if (outermost && layers.isEmpty() && skipped.isEmpty() && roots.isEmpty() && nestedHere.isEmpty()) {
            // No manifest anywhere: the whole archive is the pack, as before packs inside archives were looked for.
            layers.add(new Layer(zip, ""));
            resourcePacks.add(new Pack(null, where, null, List.of()));
        }
    }

    /**
     * Copies a nested archive out (it cannot be read in place) and looks for packs in it. The copy is made under a
     * name of the JDK's choosing in the scratch folder, through a stream that stops at the size limits; a copy that
     * fails, or that holds no resource pack, is deleted at once.
     */
    private void openNested(ZipFile zip, ZipEntry entry, String where, int depth) throws IOException {
        long declared = entry.getSize();
        long compressed = entry.getCompressedSize();
        if (declared > 0 && compressed > 0 && declared / compressed > MAX_COMPRESSION_RATIO) {
            warnings.add(describe(where) + " is compressed " + declared / compressed + " to 1, more than any pack "
                + "of textures; left out as a likely zip bomb");
            return;
        }
        long budget = Math.min(MAX_NESTED_BYTES, MAX_TOTAL_NESTED_BYTES - nestedBytes);
        if (budget <= 0 || declared > budget) {
            warnings.add(describe(where) + " is too large to unpack (at most " + MAX_NESTED_BYTES / (1024 * 1024)
                + " MB per nested archive and " + MAX_TOTAL_NESTED_BYTES / (1024 * 1024) + " MB in all); left out");
            return;
        }
        Files.createDirectories(scratch);
        Path copy = Files.createTempFile(scratch, "nested", ".zip").toAbsolutePath().normalize();
        if (!copy.startsWith(scratch)) {
            Files.deleteIfExists(copy);
            throw new IOException("temporary copy outside the scratch folder: " + copy);
        }
        long written;
        try (InputStream in = zip.getInputStream(entry); OutputStream out = Files.newOutputStream(copy)) {
            written = copyBounded(in, out, budget, where);
        } catch (IOException e) {
            Files.deleteIfExists(copy);
            warnings.add(describe(where) + " could not be unpacked within the limits (" + e.getMessage()
                + "); left out");
            return;
        }
        nestedBytes += written;
        ZipFile nested;
        try {
            nested = new ZipFile(copy.toFile());
        } catch (IOException e) {
            Files.deleteIfExists(copy);
            warnings.add(describe(where) + " is not a readable archive; left out");
            return;
        }
        int layersBefore = layers.size();
        opened.add(nested);
        extracted.add(copy);
        discover(nested, where, depth, false);
        if (layers.size() == layersBefore) {
            // Nothing in it is read later (a behavior pack, say): free the copy now rather than after the conversion.
            opened.remove(nested);
            extracted.remove(copy);
            nested.close();
            Files.deleteIfExists(copy);
        }
    }

    /** Copies at most {@code limit} bytes, failing past that; returns the bytes copied. */
    private static long copyBounded(InputStream in, OutputStream out, long limit, String what) throws IOException {
        byte[] buffer = new byte[64 * 1024];
        long total = 0;
        int read;
        while ((read = in.read(buffer)) > 0) {
            total += read;
            if (total > limit) {
                throw new IOException(what + " unpacks to more than " + limit + " bytes");
            }
            out.write(buffer, 0, read);
        }
        return total;
    }

    private static String describe(String where) {
        return where.isEmpty() ? "the file" : where;
    }

    private void addPack(ZipFile zip, String root, String where) {
        JsonObject manifest = null;
        try {
            ZipEntry entry = zip.getEntry(root + "manifest.json");
            if (entry.getSize() <= MAX_ENTRY_BYTES) {
                try (InputStream in = zip.getInputStream(entry)) {
                    ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                    copyBounded(in, bytes, MAX_ENTRY_BYTES, root + "manifest.json");
                    // Bedrock tolerates comments in its JSON; Gson's lenient parser does too.
                    manifest = JsonParser.parseString(bytes.toString(StandardCharsets.UTF_8)).getAsJsonObject();
                }
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
            // A folder name that climbs out ("..") or is a path of its own is not a subpack folder.
            if (folder.isEmpty() || folder.contains("/") || folder.contains("\\") || folder.equals("..")) {
                continue;
            }
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
        return lower.endsWith(".mcpack") || lower.endsWith(".mcaddon");
    }
}
