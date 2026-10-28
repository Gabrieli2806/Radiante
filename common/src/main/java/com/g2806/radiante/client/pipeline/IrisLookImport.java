package com.g2806.radiante.client.pipeline;

import com.g2806.radiante.client.RadianteClient;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.function.Function;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Turns the look of an Iris or OptiFine pack into a Radiante pack: Radiante keeps tracing the light, and the new pack
 * takes the raster pack's look settings - colour grade, fog, water, clouds - as values for Radiante's own hooks and
 * settings.
 *
 * <p>Nothing of the raster pack's code is run or copied. Its shaders draw the world with rasterised passes, shadow
 * maps and screen-space tricks that a path tracer has no use for, and their licences (Complementary's among them) do
 * not allow their code into a GPL pack. What is read are the values of its options: the pack's defaults, from its
 * {@code #define NAME value //[choices]} lines, with the player's own choices from Iris's {@code <pack>.txt} over
 * them. A profile maps the options of a known pack to Radiante's settings; any other pack gets the neutral look, ready
 * to be tuned in Radiante's settings. The new pack is a folder that extends radiante-pt, with hook files Radiante
 * ships ({@code assets/radiante/iris_look/}).
 */
public final class IrisLookImport {

    private static final String TEMPLATE = "/assets/radiante/iris_look/";
    private static final String[] TEMPLATE_FILES = {
        "hooks/post_grade.glsl", "hooks/fog.glsl", "hooks/sky.glsl", "hooks/water.glsl",
        "lang/en_us.lang", "lang/es_es.lang", "lang/zh_cn.lang"};
    private static final String ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.";
    /** An option line: {@code #define NAME value //[choices]}, the form Iris lists as a setting. */
    private static final Pattern OPTION = Pattern.compile("^\\s*#define\\s+(\\w+)\\s+(\\S+)\\s*//\\s*\\[");
    private static final long LARGEST_SOURCE = 2L << 20;
    private static final int MOST_SOURCES = 2000;

    /** What the import made: the new pack's path for the pipeline, and which profile was used (null: neutral). */
    public record Result(String path, String profile) {
    }

    private IrisLookImport() {
    }

    /** The game's own shader pack folder, where Iris reads its packs. */
    public static Path irisFolder() {
        return RadianteClient.radianceDir == null ? null : RadianteClient.radianceDir.getParent().resolve("shaderpacks");
    }

    /** Makes (or makes again) the Radiante pack for the raster pack at {@code pack}, named after it. */
    public static Result convert(Path pack, String displayName) throws IOException {
        Map<String, String> options = readOptions(pack);
        Path settings = pack.resolveSibling(pack.getFileName() + ".txt");
        if (Files.isRegularFile(settings)) {
            // The player's choices in Iris's settings, over the pack's defaults.
            for (String line : Files.readAllLines(settings, StandardCharsets.UTF_8)) {
                int at = line.indexOf('=');
                if (at > 0 && !line.startsWith("#")) {
                    options.put(line.substring(0, at).trim(), line.substring(at + 1).trim());
                }
            }
        }
        Profile profile = Profile.find(pack, displayName, options);
        Look look = profile == null ? new Look() : profile.look(options);

        String name = displayName.replaceAll("[^A-Za-z0-9._-]+", "-").replaceAll("^-+|-+$", "");
        Path folder = ShaderPackLibrary.folder();
        if (folder == null) {
            throw new IOException("Radiante's shader pack folder is not known yet");
        }
        Path target = folder.resolve((name.isEmpty() ? "iris-pack" : name) + "-radiante-look");
        Files.createDirectories(target.resolve("hooks"));
        Files.createDirectories(target.resolve("lang"));
        for (String file : TEMPLATE_FILES) {
            try (InputStream in = IrisLookImport.class.getResourceAsStream(TEMPLATE + file)) {
                if (in == null) {
                    throw new IOException("Radiante's look template lacks " + file);
                }
                Files.write(target.resolve(file), in.readAllBytes());
            }
        }
        Files.writeString(target.resolve("configs.json"), configs(displayName, profile, look), StandardCharsets.UTF_8);
        RadianteClient.LOGGER.info("Shader pack look: {} -> {} ({})", pack, target,
            profile == null ? "neutral" : profile.name);
        return new Result(ShaderPackLibrary.FOLDER + "/" + target.getFileName(), profile == null ? null : profile.name);
    }

    // ---- the raster pack's options ----

    /** Every option the pack's sources define, by name, with its default; the first definition of a name wins. */
    static Map<String, String> readOptions(Path pack) throws IOException {
        Map<String, String> options = new HashMap<>();
        Function<String, Boolean> source = name -> {
            String lower = name.toLowerCase(Locale.ROOT);
            return lower.contains("shaders/") && (lower.endsWith(".glsl") || lower.endsWith(".fsh")
                || lower.endsWith(".vsh") || lower.endsWith(".csh") || lower.endsWith(".gsh"));
        };
        int read = 0;
        if (Files.isDirectory(pack)) {
            try (Stream<Path> files = Files.walk(pack, 8)) {
                for (Path file : (Iterable<Path>) files::iterator) {
                    if (read >= MOST_SOURCES) {
                        break;
                    }
                    String relative = pack.relativize(file).toString().replace('\\', '/');
                    if (Files.isRegularFile(file) && source.apply(relative) && Files.size(file) <= LARGEST_SOURCE) {
                        collect(Files.readString(file, StandardCharsets.UTF_8), options);
                        read++;
                    }
                }
            }
            return options;
        }
        try (ZipFile zip = new ZipFile(pack.toFile())) {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements() && read < MOST_SOURCES) {
                ZipEntry entry = entries.nextElement();
                if (entry.isDirectory() || !source.apply(entry.getName()) || entry.getSize() > LARGEST_SOURCE) {
                    continue;
                }
                try (InputStream in = zip.getInputStream(entry)) {
                    // Bounded by what was read, not by the header's size.
                    byte[] bytes = in.readNBytes((int) LARGEST_SOURCE);
                    collect(new String(bytes, StandardCharsets.UTF_8), options);
                }
                read++;
            }
        }
        return options;
    }

    private static void collect(String text, Map<String, String> options) {
        for (String line : text.split("\n")) {
            Matcher matcher = OPTION.matcher(line);
            if (matcher.find()) {
                options.putIfAbsent(matcher.group(1), matcher.group(2));
            }
        }
    }

    private static boolean packHas(Path pack, String text) {
        String lower = text.toLowerCase(Locale.ROOT);
        try {
            if (Files.isDirectory(pack)) {
                Path licence = pack.resolve("License.txt");
                return Files.isRegularFile(licence) && Files.size(licence) < 1 << 20
                    && Files.readString(licence, StandardCharsets.UTF_8).toLowerCase(Locale.ROOT).contains(lower);
            }
            try (ZipFile zip = new ZipFile(pack.toFile())) {
                ZipEntry entry = zip.getEntry("License.txt");
                if (entry == null) {
                    return false;
                }
                try (InputStream in = zip.getInputStream(entry)) {
                    return new String(in.readNBytes(1 << 20), StandardCharsets.UTF_8).toLowerCase(Locale.ROOT)
                        .contains(lower);
                }
            }
        } catch (IOException | RuntimeException e) {
            return false;
        }
    }

    private static double number(Map<String, String> options, String name, double fallback) {
        try {
            String value = options.get(name);
            return value == null ? fallback : Double.parseDouble(value);
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static double clamp(double value, double low, double high) {
        return Math.max(low, Math.min(high, value));
    }

    // ---- the look ----

    /**
     * The values the template's hooks and Radiante's settings take. The neutral look changes nothing; a profile moves
     * them towards the raster pack. Fields left null keep radiante-pt's own default.
     */
    static final class Look {
        double saturation = 1.0;
        double darkDesaturation = 0.0;
        double warmth = 0.0;
        double contrast = 0.0;
        double vignette = 0.0;
        double fog = 1.0;
        double skySaturation = 1.0;
        double waterAbsorption = 1.0;
        /** radiante-pt settings given a new default: name without the prefix, value as configs.json writes it. */
        final Map<String, String> radiante = new HashMap<>();
    }

    /** A known raster pack: how to find it, and how its options become a look. */
    private abstract static class Profile {
        final String name;

        Profile(String name) {
            this.name = name;
        }

        abstract boolean matches(Path pack, String displayName, Map<String, String> options);

        abstract Look look(Map<String, String> options);

        static final List<Profile> ALL = List.of(new Complementary());

        static Profile find(Path pack, String displayName, Map<String, String> options) {
            for (Profile profile : ALL) {
                if (profile.matches(pack, displayName, options)) {
                    return profile;
                }
            }
            return null;
        }
    }

    /**
     * Complementary Reimagined and Unbound. The style - vivid but soft colours, warm light, dark tones that lose their
     * colour, a light atmospheric haze, clear water, big clouds - is Radiante's own reading of how the pack looks,
     * written for Radiante's hooks; the pack's options then move it as they move the pack: Saturation, Dark
     * Desaturation, Atmospheric Fog strength and distance, Water Fog, Water Bumpiness, cloud style and amount.
     */
    private static final class Complementary extends Profile {
        Complementary() {
            super("Complementary");
        }

        @Override
        boolean matches(Path pack, String displayName, Map<String, String> options) {
            return displayName.toLowerCase(Locale.ROOT).contains("complementary")
                || packHas(pack, "Complementary License Agreement");
        }

        @Override
        Look look(Map<String, String> options) {
            Look look = new Look();
            look.saturation = clamp(1.12 * number(options, "T_SATURATION", 1.0), 0.0, 2.0);
            look.darkDesaturation = clamp(number(options, "TM_DARK_DESATURATION", 0.25), 0.0, 1.0);
            look.warmth = 0.12;
            look.contrast = 0.08;
            look.vignette = 0.15;
            // Atmospheric fog: its strength, and how far it reaches (a shorter distance is thicker fog).
            double fogStrength = number(options, "ATM_FOG_MULT", 0.95) / 0.95;
            double fogReach = 100.0 / Math.max(10.0, number(options, "ATM_FOG_DISTANCE", 100.0));
            look.fog = clamp(fogStrength * fogReach, 0.0, 4.0);
            look.radiante.put("biome_fog_scattering", String.format(Locale.ROOT, "%.2f",
                clamp(0.4 * look.fog, 0.0, 16.0)));
            look.skySaturation = 1.1;
            // Water: its fog is how quickly the view into it fades; its bumpiness the size of the waves.
            double waterFog = number(options, "WATER_FOG_MULT", 100.0) / 100.0;
            look.waterAbsorption = clamp(0.8 * waterFog, 0.0, 4.0);
            look.radiante.put("water_wave_strength", String.format(Locale.ROOT, "%.2f",
                clamp(0.35 * number(options, "WATER_BUMPINESS", 1.25) / 1.25, 0.0, 2.0)));
            // Clouds: Reimagined's and Unbound's styles are volumetric; 0 turns them off.
            double style = number(options, "CLOUD_STYLE_DEFINE", -1);
            look.radiante.put("cloud_mode", ATTRIBUTE + "cloud_mode." + (style == 0 ? "off" : "volumetric"));
            look.radiante.put("volumetric_cloud_coverage", String.format(Locale.ROOT, "%.2f",
                clamp(0.5 * number(options, "CLOUD_UNBOUND_AMOUNT", 1.0), 0.0, 1.0)));
            return look;
        }
    }

    // ---- the pack ----

    private static String configs(String displayName, Profile profile, Look look) {
        JsonObject root = new JsonObject();
        JsonObject manifest = new JsonObject();
        manifest.addProperty("display_name", displayName + " (Radiante look)");
        manifest.addProperty("version", "1");
        manifest.addProperty("author", "Radiante");
        manifest.addProperty("license", "GPL-3.0");
        manifest.addProperty("description", profile == null
            ? "Neutral look made from " + displayName + "; tune it in Radiante's settings. Light by Radiante."
            : displayName + "'s look settings on Radiante's light (" + profile.name + " profile). No code of the "
                + "original pack is used.");
        manifest.addProperty("api_version", 0);
        manifest.addProperty("extends", "radiante-pt@1");
        root.add("radiante", manifest);

        JsonArray attributes = new JsonArray();
        attributes.add(attribute("look_saturation", "float_range:0.0-2.0", look.saturation, "LOOK_SATURATION", false));
        attributes.add(attribute("look_dark_desaturation", "float_range:0.0-1.0", look.darkDesaturation,
            "LOOK_DARK_DESATURATION", false));
        attributes.add(attribute("look_warmth", "float_range:-1.0-1.0", look.warmth, "LOOK_WARMTH", false));
        attributes.add(attribute("look_contrast", "float_range:-0.5-0.5", look.contrast, "LOOK_CONTRAST", false));
        attributes.add(attribute("look_vignette", "float_range:0.0-1.0", look.vignette, "LOOK_VIGNETTE", false));
        attributes.add(attribute("look_fog", "float_range:0.0-4.0", look.fog, "LOOK_FOG", true));
        attributes.add(attribute("look_sky_saturation", "float_range:0.0-2.0", look.skySaturation,
            "LOOK_SKY_SATURATION", true));
        attributes.add(attribute("look_water_absorption", "float_range:0.0-4.0", look.waterAbsorption,
            "LOOK_WATER_ABSORPTION", false));
        // radiante-pt's own settings with a new default: merged into radiante-pt's attribute by name.
        look.radiante.forEach((name, value) -> {
            JsonObject override = new JsonObject();
            override.addProperty("name", ATTRIBUTE + name);
            override.addProperty("default_value", value);
            attributes.add(override);
        });
        root.add("attributes", attributes);

        JsonObject post = new JsonObject();
        JsonArray globals = new JsonArray();
        JsonObject grade = new JsonObject();
        grade.addProperty("RADIANTE_HOOK_POST_GRADE", true);
        globals.add(grade);
        post.add("global_variables", globals);
        root.add("execution_post", post);
        return new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create().toJson(root) + "\n";
    }

    private static JsonObject attribute(String name, String type, double value, String define, boolean runtime) {
        JsonObject attribute = new JsonObject();
        attribute.addProperty("name", ATTRIBUTE + name);
        attribute.addProperty("type", type);
        attribute.addProperty("default_value", String.format(Locale.ROOT, "%.2f", value));
        if (runtime) {
            attribute.addProperty("runtime", true);
        }
        attribute.addProperty("define", define);
        return attribute;
    }
}
