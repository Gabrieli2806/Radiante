package com.g2806.radiante.client.bedrock;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.platform.RadiantePlatform;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;
import javax.imageio.ImageIO;

/**
 * Turns Bedrock RTX resource packs ({@code .mcpack}, and the resource packs inside an {@code .mcaddon}, see
 * {@link BedrockPackFiles}) dropped into the resource pack folder into LabPBR packs the renderer reads; the pack list
 * shows the Bedrock file itself (see PackDetectorMixin) and loads the conversion. Bedrock
 * describes each texture with a texture set: a colour texture, a MER(S) map (metalness,
 * emission, roughness, subsurface) and a height or normal map, under Bedrock's own texture names. Each Java block
 * texture that has a Bedrock counterpart (see texture_map.json) gets the pack's colour, a LabPBR specular map and a
 * LabPBR normal map built from them, so the pack keeps its look: its colours, how rough, metallic and glowing
 * things are, and the relief of its surfaces. The pack's per-biome volumetric fog comes along too (see
 * {@link BedrockFog}), and so do the light colours of its Vibrant Visuals local lighting (see
 * {@link BedrockLocalLighting}).
 */
public final class BedrockPackConverter {

    /** Under Radiante's folder in the game directory: the converted packs, one zip per Bedrock file. */
    private static final String CACHE_FOLDER = "bedrock_packs";
    /** Largest size a Bedrock texture set is converted at, in pixels per side; finer maps are averaged down to it. */
    private static final int MAX_DETAIL = 128;
    /** Stored as the zip comment; a pack converted by another version of the converter is converted again. */
    private static final String CONVERTER_VERSION = "radiante-bedrock-converter 21";

    /** The converter's version, for the log and the settings screen; packs converted by another are converted again. */
    public static String converterVersion() {
        return CONVERTER_VERSION.substring(CONVERTER_VERSION.lastIndexOf(' ') + 1);
    }
    /** Resource pack format of Minecraft 26.3. */
    private static final int PACK_FORMAT = 97;
    /** Slope of normals built from a height map: height units per texel. */
    private static final float HEIGHT_TO_NORMAL = 2.0f;
    /**
     * LabPBR height of every texel: the surface itself. Bedrock only shades with its height maps and never displaces
     * by them, and they are authored for that - mortar lines drop from white to black in one texel - so parallax
     * would dig trenches the pack never had.
     */
    private static final int FLAT_HEIGHT = 255;
    /** LabPBR F0 of non-metals (reflectance 0.04), stored in the specular map's green channel. */
    private static final int DIELECTRIC_F0 = 10;
    /** LabPBR green channel for a metal that reflects its own colour. */
    private static final int ALBEDO_METAL = 255;
    /**
     * Bedrock multiplies emission by a strong constant of its own: RTX packs give full light sources (torches,
     * glowstone, sea lanterns) about a quarter of the range. Radiante lights them at full strength, so the same
     * ratio lands those at the top of LabPBR's range.
     */
    private static final int EMISSION_SCALE = 4;

    /**
     * Where the original MER(S) data of every converted texture is kept, one PNG per Java texture name at the
     * authored resolution (a flat-value material is a 1 x 1 image), plus {@link #RAW_MER_INDEX}. Outside
     * {@code textures/}, so the game's atlases do not pick them up as sprites. See {@link MaterialEncoding}.
     */
    public static final String RAW_MER_DIR = "assets/radiante/bedrock_mer/";
    public static final String RAW_MER_INDEX = RAW_MER_DIR + "index.json";

    /** The raw MER(S) textures written by the conversion in progress; conversions are serialised. */
    private static JsonObject rawMerIndex = new JsonObject();

    private BedrockPackConverter() {
    }

    /**
     * The LabPBR pack for a Bedrock .mcpack or .mcaddon, converted the first time it is asked for and kept in
     * Radiante's own folder (not beside the Bedrock file, where it would show up in the pack list a second time), or
     * null if it cannot be or holds no resource pack.
     */
    public static synchronized Path converted(Path pack) {
        String name = pack.getFileName().toString();
        Path folder = RadiantePlatform.INSTANCE.gameDir().resolve("radiante").resolve(CACHE_FOLDER);
        Path out = folder.resolve(cacheName(name));
        try {
            if (isUpToDate(out, pack)) {
                return out;
            }
            JsonObject textureMap = loadTextureMap();
            if (textureMap == null) {
                return null;
            }
            Files.createDirectories(out.getParent());
            long start = System.nanoTime();
            try (BedrockPackFiles files = BedrockPackFiles.open(pack, folder.resolve(".nested"))) {
                for (BedrockPackFiles.Pack skipped : files.skipped()) {
                    // Scripts (JavaScript on Bedrock's script API), functions and custom blocks run only in Bedrock's
                    // engine; there is nothing on Java to hand them to.
                    RadianteClient.LOGGER.info("Skipped {} in Bedrock pack {} ({}): its {} modules only run in "
                        + "Bedrock; behavior, script and data packs cannot run on Java", describe(skipped), name,
                        skipped.where().isEmpty() ? "/" : skipped.where(), String.join(", ", skipped.moduleTypes()));
                }
                if (files.resourcePacks().isEmpty()) {
                    RadianteClient.LOGGER.warn("Bedrock pack {} holds no resource pack, nothing to convert", name);
                    return null;
                }
                for (BedrockPackFiles.Pack resources : files.resourcePacks()) {
                    RadianteClient.LOGGER.info("Reading {} in Bedrock pack {} ({}){}", describe(resources), name,
                        resources.where().isEmpty() ? "/" : resources.where(),
                        resources.subpack() == null ? "" : ", subpack " + resources.subpack());
                }
                int converted = convert(files, name, out, textureMap);
                RadianteClient.LOGGER.info("Converted Bedrock pack {} ({} textures) in {} ms", name, converted,
                    (System.nanoTime() - start) / 1_000_000);
            }
            return out;
        } catch (Exception e) {
            RadianteClient.LOGGER.warn("Could not convert Bedrock pack {}", name, e);
            return null;
        }
    }

    /**
     * The converted pack's file name. A .mcpack keeps the name it always had; an .mcaddon of the same name gets one of
     * its own.
     */
    static String cacheName(String fileName) {
        String lower = fileName.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".mcpack")) {
            return fileName.substring(0, fileName.length() - ".mcpack".length()) + ".zip";
        }
        if (lower.endsWith(".mcaddon")) {
            return fileName.substring(0, fileName.length() - ".mcaddon".length()) + ".mcaddon.zip";
        }
        return fileName + ".zip";
    }

    private static String describe(BedrockPackFiles.Pack pack) {
        return pack.name() == null ? "the pack" : "\"" + pack.name() + "\"";
    }

    public static boolean isBedrockPack(Path path) {
        String name = path.getFileName().toString().toLowerCase(Locale.ROOT);
        return name.endsWith(".mcpack") || name.endsWith(".mcaddon");
    }

    private static boolean isUpToDate(Path out, Path pack) throws IOException {
        if (!Files.exists(out) || Files.getLastModifiedTime(out).compareTo(Files.getLastModifiedTime(pack)) < 0) {
            return false;
        }
        try (ZipFile zip = new ZipFile(out.toFile())) {
            return CONVERTER_VERSION.equals(zip.getComment());
        } catch (IOException e) {
            return false;
        }
    }

    private static JsonObject loadTextureMap() {
        try (InputStream in =
                 BedrockPackConverter.class.getResourceAsStream("/assets/radiante/bedrock/texture_map.json")) {
            if (in == null) {
                RadianteClient.LOGGER.warn("Bedrock texture map missing from the Radiante jar");
                return null;
            }
            return JsonParser.parseReader(new InputStreamReader(in, StandardCharsets.UTF_8)).getAsJsonObject()
                .getAsJsonObject("textures");
        } catch (IOException e) {
            RadianteClient.LOGGER.warn("Could not read the Bedrock texture map", e);
            return null;
        }
    }

    private static int convert(BedrockPackFiles files, String fileName, Path out, JsonObject textureMap)
        throws IOException {
        Path temp = out.resolveSibling(out.getFileName() + ".tmp");
        int converted = 0;
        rawMerIndex = new JsonObject();
        try (ZipOutputStream zos = new ZipOutputStream(Files.newOutputStream(temp))) {
            zos.setComment(CONVERTER_VERSION);
            // The topmost resource pack's own name; a pack without one is named after the file.
            String name = files.name() != null ? files.name()
                : fileName.substring(0, Math.max(0, fileName.lastIndexOf('.')));

            write(zos, "pack.mcmeta", ("{\n  \"pack\": {\n    \"description\": \"" + escape(name)
                + " - converted from Bedrock by Radiante\",\n    \"min_format\": [" + PACK_FORMAT
                + ", 0],\n    \"max_format\": [" + PACK_FORMAT + ", 999]\n  }\n}\n").getBytes(StandardCharsets.UTF_8));
            byte[] icon = files.read("pack_icon.png");
            if (icon != null) {
                write(zos, "pack.png", icon);
            }

            for (Map.Entry<String, JsonElement> entry : textureMap.entrySet()) {
                JsonObject info = entry.getValue().getAsJsonObject();
                String bedrockPath = info.get("bedrock").getAsString();
                boolean colorCompatible = info.get("color").getAsBoolean();
                try {
                    if (convertTexture(files, entry.getKey(), bedrockPath, colorCompatible, zos)) {
                        converted++;
                        // An animated texture (water) is animated by its .mcmeta, which has to come from the same
                        // pack as the image: without it Java showed the pack's strip of frames as one tall texture.
                        if (colorCompatible && info.has("mcmeta") && color(files, bedrockPath) != null) {
                            write(zos, "assets/minecraft/textures/block/" + entry.getKey() + ".png.mcmeta",
                                info.get("mcmeta").toString().getBytes(StandardCharsets.UTF_8));
                        }
                    }
                } catch (Exception e) {
                    RadianteClient.LOGGER.debug("Skipped Bedrock texture {}: {}", bedrockPath, e.toString());
                }
            }

            // The side of a grass block is two layers in Java: the dirt with a green fringe painted on, and the
            // tinted fringe over it. The fringe comes from the pack and has the pack's shape; Java's painted one
            // under it has another, and its tips showed past the pack's as dark green slits. Plain dirt goes under
            // the pack's fringe instead.
            TgaReader.Image dirt = color(files, "textures/blocks/dirt");
            if (dirt != null) {
                write(zos, "assets/minecraft/textures/block/grass_block_side.png",
                    png(grassSideBase(dirt, color(files, "textures/blocks/grass_side"))));
            }

            JsonObject rawIndex = new JsonObject();
            rawIndex.addProperty("encoding", MaterialEncoding.BEDROCK_MER.name());
            rawIndex.addProperty("channels", "R metalness, G emissive, B roughness, all 0-255 as authored; A the "
                + "height map (1 + height x 253, height 0-1 as authored) for a texture shaded by one, 255 for any other");
            rawIndex.add("textures", rawMerIndex);
            write(zos, RAW_MER_INDEX, rawIndex.toString().getBytes(StandardCharsets.UTF_8));

            byte[] fog = BedrockFog.convert(files);
            if (fog != null) {
                write(zos, BedrockFog.PATH, fog);
            }
            byte[] lights = BedrockLocalLighting.convert(files);
            if (lights != null) {
                write(zos, BedrockLocalLighting.PATH, lights);
            }
        } catch (IOException | RuntimeException e) {
            Files.deleteIfExists(temp);
            throw e;
        }
        Files.move(temp, out, StandardCopyOption.REPLACE_EXISTING);
        return converted;
    }

    /** Grass green of the plains, for the part of the side that is always under the fringe. */
    private static final int GRASS_TINT = 0x91BD59;

    /**
     * The layer under a grass block's fringe: the pack's dirt, and where the fringe covers a whole row - the top
     * of the side - the fringe's own grass in a plain green, so that the hair of it that shows along the block's
     * top edge, where the fringe is sampled a little short, is green and not a line of dirt.
     */
    private static TgaReader.Image grassSideBase(TgaReader.Image dirt, TgaReader.Image fringe) {
        if (fringe == null) {
            return dirt;
        }
        int width = dirt.width();
        int height = dirt.height();
        int[] base = dirt.argb().clone();
        int[] grass = fringe.argb();
        for (int y = 0; y < height; y++) {
            int y0 = y * fringe.height() / height;
            int y1 = Math.max(y0 + 1, (y + 1) * fringe.height() / height);
            boolean covered = true;
            for (int fy = y0; fy < y1 && covered; fy++) {
                for (int fx = 0; fx < fringe.width(); fx++) {
                    if (grass[fy * fringe.width() + fx] >>> 24 < 250) {
                        covered = false;
                        break;
                    }
                }
            }
            if (!covered) {
                break;
            }
            for (int x = 0; x < width; x++) {
                int texel = grass[y0 * fringe.width() + x * fringe.width() / width];
                // The fringe is grey for the tint to colour; at full brightness here, it reads as lit grass.
                int level = Math.max(1, Math.max(texel >> 16 & 0xFF, Math.max(texel >> 8 & 0xFF, texel & 0xFF)));
                int r = (texel >> 16 & 0xFF) * (GRASS_TINT >> 16 & 0xFF) / level;
                int g = (texel >> 8 & 0xFF) * (GRASS_TINT >> 8 & 0xFF) / level;
                int b = (texel & 0xFF) * (GRASS_TINT & 0xFF) / level;
                base[y * width + x] = 0xFF000000 | r << 16 | g << 8 | b;
            }
        }
        return new TgaReader.Image(width, height, base);
    }

    /** The colour image of a Bedrock texture set, or null when the pack has none. */
    private static TgaReader.Image color(BedrockPackFiles files, String bedrockPath) throws IOException {
        byte[] setBytes = files.read(bedrockPath + ".texture_set.json");
        if (setBytes == null) {
            return null;
        }
        JsonObject set = JsonParser.parseString(new String(setBytes, StandardCharsets.UTF_8)).getAsJsonObject()
            .getAsJsonObject("minecraft:texture_set");
        if (set == null) {
            return null;
        }
        String dir = bedrockPath.contains("/") ? bedrockPath.substring(0, bedrockPath.lastIndexOf('/') + 1) : "";
        return image(files, dir, set.get("color"));
    }

    /** One Java texture from its Bedrock texture set; false when the pack has no set for it. */
    private static boolean convertTexture(BedrockPackFiles files, String javaName, String bedrockPath,
        boolean colorCompatible, ZipOutputStream zos) throws IOException {
        byte[] setBytes = files.read(bedrockPath + ".texture_set.json");
        if (setBytes == null) {
            return false;
        }
        JsonObject set = JsonParser.parseString(new String(setBytes, StandardCharsets.UTF_8)).getAsJsonObject()
            .getAsJsonObject("minecraft:texture_set");
        if (set == null) {
            return false;
        }
        String dir = bedrockPath.contains("/") ? bedrockPath.substring(0, bedrockPath.lastIndexOf('/') + 1) : "";

        TgaReader.Image color = image(files, dir, set.get("color"));
        JsonElement merSource = set.has("metalness_emissive_roughness_subsurface")
            ? set.get("metalness_emissive_roughness_subsurface") : set.get("metalness_emissive_roughness");
        boolean hasSubsurface = set.has("metalness_emissive_roughness_subsurface");
        TgaReader.Image mer = image(files, dir, merSource);
        TgaReader.Image height = image(files, dir, set.get("heightmap"));
        TgaReader.Image normal = image(files, dir, set.get("normal"));

        // Bedrock RTX packs pair 16 px colours with far finer normal and MER maps (96 px is common) and shade with
        // them at full detail. Squeezed down to the colour's size by picking one texel in six, a bevelled metal
        // block kept only its steepest edge normals and reflected black in patches. When the colour may replace
        // Java's, it is scaled up instead (nearest, so it looks the same) and the maps keep their detail.
        int detail = Math.max(mer != null ? mer.width() : 0,
            Math.max(normal != null ? normal.width() : 0, height != null ? height.width() : 0));
        if (color != null && colorCompatible && detail > color.width() && detail % color.width() == 0) {
            int scale = Math.min(detail, MAX_DETAIL) / color.width();
            if (scale > 1) {
                color = color.resized(color.width() * scale, color.height() * scale);
            }
        }
        int width = color != null ? color.width() : mer != null ? mer.width() : 16;
        int heightPx = color != null ? color.height() : mer != null ? mer.height() : 16;
        String target = "assets/minecraft/textures/block/" + javaName;

        if (color != null && colorCompatible) {
            write(zos, target + ".png", png(color));
        }

        int[] merUniform = uniform(merSource);
        // Bedrock shades a set by its normal map when it has one, by its height map only otherwise.
        keepRawMer(zos, javaName, mer, merUniform, hasSubsurface, normal == null ? height : null);
        if (mer != null || merUniform != null) {
            TgaReader.Image merImage = mer != null ? mer.averaged(width, heightPx) : null;
            int[] specular = new int[width * heightPx];
            for (int i = 0; i < specular.length; i++) {
                int m, e, r, s;
                if (merImage != null) {
                    int p = merImage.argb()[i];
                    m = p >> 16 & 0xFF;
                    e = p >> 8 & 0xFF;
                    r = p & 0xFF;
                    s = hasSubsurface ? p >>> 24 : 0;
                } else {
                    m = merUniform[0];
                    e = merUniform[1];
                    r = merUniform[2];
                    s = hasSubsurface && merUniform.length > 3 ? merUniform[3] : 0;
                }
                specular[i] = labPbrSpecular(m, e, r, s);
            }
            write(zos, target + "_s.png", png(new TgaReader.Image(width, heightPx, specular)));
        }

        if (normal != null) {
            write(zos, target + "_n.png", png(labPbrFromNormal(normal.averaged(width, heightPx))));
        } else if (height != null) {
            if (!isGreyscale(height) && packedHeightWarnings++ < 5) {
                RadianteClient.LOGGER.warn("Bedrock height map of {} is not greyscale: it may be the packed height "
                    + "and edge-normal form, which is not decoded (it is read as a plain height)", bedrockPath);
            }
            // The colour's holes (alpha 0) carry no height of their own: leaves are painted black there, and the
            // cliff from a leaf down to a hole made every leaf texel's edge a steep normal that sparkled.
            TgaReader.Image coverage = color != null && color.width() == width && color.height() == heightPx ? color : null;
            write(zos, target + "_n.png", png(labPbrFromHeight(height.averaged(width, heightPx), coverage)));
        }
        return true;
    }

    /** How many height maps that are not greyscale were reported; a pack of them would flood the log. */
    private static int packedHeightWarnings;

    /**
     * True when the image carries one grey value per texel (red, green and blue equal), as every height map seen in
     * Vanilla RTX and in Kelly's RTX pack does, even when stored as four channels. A map that is not is something
     * else (Bedrock's engine also knows a height map packed together with edge normals); reading it as a height
     * would give wrong normals, so the caller says so.
     */
    static boolean isGreyscale(TgaReader.Image image) {
        for (int p : image.argb()) {
            int r = p >> 16 & 0xFF;
            int g = p >> 8 & 0xFF;
            int b = p & 0xFF;
            if (Math.abs(r - g) > 2 || Math.abs(g - b) > 2) {
                return false;
            }
        }
        return true;
    }

    /**
     * Keeps the texture's MER(S) data exactly as authored. The LabPBR maps made from it are approximations (emission
     * is scaled and clamped, roughness and metalness are re-encoded); this copy is what a Bedrock-faithful profile
     * must be able to read back.
     *
     * <p>Its alpha carries the set's height map, when Bedrock shades the set by one: 1 + height x 253 (height 0 to 1,
     * the map's red channel, as Bedrock reads it), and 255 for every other texture. 0 stays "no Bedrock material here"
     * once stitched into the atlas. The LabPBR normal map made from the heights is a smooth slope across every texel;
     * Bedrock shades a height map as flat texels with a narrow bevel along their edges, and needs the heights for it.
     * (The subsurface channel a MERS map carries is not kept here: nothing reads it from this copy.)
     */
    private static void keepRawMer(ZipOutputStream zos, String javaName, TgaReader.Image mer, int[] uniform,
        boolean subsurface, TgaReader.Image heightMap) throws IOException {
        TgaReader.Image raw;
        JsonObject entry = new JsonObject();
        if (mer != null) {
            raw = mer;
        } else if (uniform != null) {
            raw = new TgaReader.Image(1, 1, new int[] {uniform[0] << 16 | uniform[1] << 8 | uniform[2]});
            entry.addProperty("uniform", heightMap == null);
        } else {
            return;
        }
        if (heightMap != null) {
            // At the finer of the two resolutions: the bevels follow the height map's own texels.
            int width = Math.max(raw.width(), heightMap.width());
            int height = Math.max(raw.height(), heightMap.height());
            TgaReader.Image material = raw.resized(width, height);
            TgaReader.Image heights = heightMap.averaged(width, height);
            int[] out = new int[width * height];
            for (int i = 0; i < out.length; i++) {
                int red = heights.argb()[i] >> 16 & 0xFF;
                int encoded = 1 + Math.round(red / 255.0f * 253.0f);
                out[i] = encoded << 24 | (material.argb()[i] & 0xFFFFFF);
            }
            raw = new TgaReader.Image(width, height, out);
            entry.addProperty("height_map", true);
        } else {
            int[] out = new int[raw.argb().length];
            for (int i = 0; i < out.length; i++) {
                out[i] = 0xFF000000 | (raw.argb()[i] & 0xFFFFFF);
            }
            raw = new TgaReader.Image(raw.width(), raw.height(), out);
        }
        entry.addProperty("width", raw.width());
        entry.addProperty("height", raw.height());
        entry.addProperty("subsurface", subsurface);
        rawMerIndex.add(javaName, entry);
        write(zos, RAW_MER_DIR + javaName + ".png", png(raw));
    }

    /** LabPBR specular texel from Bedrock metalness, emission, roughness and subsurface (all 0-255). */
    static int labPbrSpecular(int metalness, int emission, int roughness, int subsurface) {
        // Bedrock shades with GGX alpha = roughness^2. LabPBR's roughness is (1 - smoothness)^2 and the shading
        // squares it again, so smoothness = 1 - sqrt(roughness) gives Bedrock's alpha exactly. Stored as
        // 1 - roughness, every surface came out far glossier than in Bedrock: leaves sparkled, stone shone.
        int smoothness = 255 - (int) Math.round(Math.sqrt(roughness / 255.0) * 255.0);
        // LabPBR has no partial metals. Almost none is a 4 % dielectric and almost full reflects its colour; in
        // between goes to 238-254, which LabPBR leaves unused and Radiante's shaders read as partly metallic
        // ((value - 237) / 18), as Bedrock blends it. Gems are painted around half metal.
        int f0;
        if (metalness < 14) {
            f0 = DIELECTRIC_F0;
        } else if (metalness > 241) {
            f0 = ALBEDO_METAL;
        } else {
            f0 = 237 + Math.max(1, Math.min(17, Math.round(metalness / 255.0f * 18.0f)));
        }
        int porositySss = subsurface > 0 ? 65 + subsurface * 190 / 255 : 0;
        int emissive = emission > 0 ? Math.min(254, emission * EMISSION_SCALE) : 255;
        return emissive << 24 | smoothness << 16 | f0 << 8 | porositySss;
    }

    /** LabPBR normal map from a Bedrock height map: normal in red and green, full AO in blue, flat height in alpha. */
    static TgaReader.Image labPbrFromHeight(TgaReader.Image height) {
        return labPbrFromHeight(height, null);
    }

    /** As above; a neighbour where {@code coverage} is transparent counts as level with the texel itself. */
    static TgaReader.Image labPbrFromHeight(TgaReader.Image height, TgaReader.Image coverage) {
        int w = height.width();
        int h = height.height();
        int[] out = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float centre = grey(height.get(x, y));
                float du = (heightOrCentre(height, coverage, x + 1, y, centre)
                    - heightOrCentre(height, coverage, x - 1, y, centre)) * 0.5f;
                // Texture v runs up, image rows run down.
                float dv = (heightOrCentre(height, coverage, x, y - 1, centre)
                    - heightOrCentre(height, coverage, x, y + 1, centre)) * 0.5f;
                out[y * w + x] = encodeNormal(-du * HEIGHT_TO_NORMAL, -dv * HEIGHT_TO_NORMAL, 1.0f);
            }
        }
        return new TgaReader.Image(w, h, out);
    }

    private static float heightOrCentre(TgaReader.Image height, TgaReader.Image coverage, int x, int y, float centre) {
        if (coverage != null && (coverage.get(x, y) >>> 24) == 0) {
            return centre;
        }
        return grey(height.get(x, y));
    }

    /** A Bedrock normal map re-encoded for LabPBR. */
    static TgaReader.Image labPbrFromNormal(TgaReader.Image normal) {
        int[] out = new int[normal.argb().length];
        for (int i = 0; i < out.length; i++) {
            int p = normal.argb()[i];
            float nx = (p >> 16 & 0xFF) / 127.5f - 1.0f;
            float ny = (p >> 8 & 0xFF) / 127.5f - 1.0f;
            // Bedrock RTX reads only red and green and rebuilds Z from them. Normalising the authored RGB as a
            // vector instead (blue is often left at full, or holds something else) shortened or tilted every
            // normal: ore specks and metal bevels reflected the wrong way. RG is kept as authored; only a pair
            // longer than one is brought back to the unit circle.
            float length = (float) Math.sqrt(nx * nx + ny * ny);
            if (length > 1.0f) {
                nx /= length;
                ny /= length;
            }
            int r = Math.round((nx * 0.5f + 0.5f) * 255.0f);
            int g = Math.round((ny * 0.5f + 0.5f) * 255.0f);
            out[i] = FLAT_HEIGHT << 24 | r << 16 | g << 8 | 255;
        }
        return new TgaReader.Image(normal.width(), normal.height(), out);
    }

    private static int encodeNormal(float x, float y, float z) {
        float length = (float) Math.sqrt(x * x + y * y + z * z);
        int r = Math.round((x / length * 0.5f + 0.5f) * 255.0f);
        int g = Math.round((y / length * 0.5f + 0.5f) * 255.0f);
        return FLAT_HEIGHT << 24 | r << 16 | g << 8 | 255;
    }

    private static float grey(int argb) {
        return ((argb >> 16 & 0xFF) + (argb >> 8 & 0xFF) + (argb & 0xFF)) / (3.0f * 255.0f);
    }

    /** A texture set layer: an image by name next to the set, or null for a missing or uniform layer. */
    private static TgaReader.Image image(BedrockPackFiles files, String dir, JsonElement layer) throws IOException {
        if (layer == null || !layer.isJsonPrimitive() || !layer.getAsJsonPrimitive().isString()) {
            return null;
        }
        String value = layer.getAsString();
        if (value.startsWith("#")) {
            return null;
        }
        byte[] tga = files.read(dir + value + ".tga");
        if (tga != null) {
            return TgaReader.read(tga);
        }
        byte[] png = files.read(dir + value + ".png");
        if (png == null) {
            png = files.read(dir + value + ".jpg");
        }
        if (png == null) {
            return null;
        }
        BufferedImage buffered = ImageIO.read(new ByteArrayInputStream(png));
        if (buffered == null) {
            return null;
        }
        int w = buffered.getWidth();
        int h = buffered.getHeight();
        return new TgaReader.Image(w, h, buffered.getRGB(0, 0, w, h, null, 0, w));
    }

    /** A layer given as one value for the whole texture: [r, g, b(, a)] or "#rrggbb(aa)". */
    private static int[] uniform(JsonElement layer) {
        if (layer == null) {
            return null;
        }
        if (layer.isJsonArray()) {
            JsonArray array = layer.getAsJsonArray();
            int[] values = new int[array.size()];
            for (int i = 0; i < values.length; i++) {
                values[i] = Math.clamp(Math.round(array.get(i).getAsFloat()), 0, 255);
            }
            return values.length >= 3 ? values : null;
        }
        if (layer.isJsonPrimitive() && layer.getAsString().startsWith("#")) {
            String hex = layer.getAsString().substring(1);
            int[] values = new int[hex.length() / 2];
            for (int i = 0; i < values.length; i++) {
                values[i] = Integer.parseInt(hex.substring(i * 2, i * 2 + 2), 16);
            }
            return values.length >= 3 ? values : null;
        }
        return null;
    }

    private static byte[] png(TgaReader.Image image) throws IOException {
        BufferedImage buffered = new BufferedImage(image.width(), image.height(), BufferedImage.TYPE_INT_ARGB);
        buffered.setRGB(0, 0, image.width(), image.height(), image.argb(), 0, image.width());
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        ImageIO.write(buffered, "png", out);
        return out.toByteArray();
    }

    private static void write(ZipOutputStream zos, String path, byte[] data) throws IOException {
        zos.putNextEntry(new ZipEntry(path));
        zos.write(data);
        zos.closeEntry();
    }

    private static String escape(String text) {
        return text.replace("\\", "\\\\").replace("\"", "\\\"");
    }
}
