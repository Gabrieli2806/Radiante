package com.g2806.radiante.client.bedrock;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/** Finding the resource packs inside Bedrock archives (.mcaddon, .mcpack) and reading their local lighting. */
class BedrockPackFilesTest {

    @TempDir
    Path temp;

    private static final String RP_MANIFEST = """
        {"format_version": 2, "header": {"name": "Lights §6Pack"},
         "modules": [{"type": "resources"}],
         "subpacks": [{"folder_name": "missing", "name": "Not shipped", "memory_tier": 1},
                      {"folder_name": "plain", "name": "Plain", "memory_tier": 1},
                      {"folder_name": "fancy", "name": "Fancy", "memory_tier": 2}]}
        """;

    private static final String BP_MANIFEST = """
        {"format_version": 2, "header": {"name": "Lights Behavior"},
         "modules": [{"type": "data"}, {"type": "script", "language": "javascript", "entry": "scripts/main.js"}]}
        """;

    /** Vazah's Dynamic Lights layout: bp/ and rp/ folders, subpacks with local lighting in the resource pack. */
    @Test
    void addonWithPackFolders() throws IOException {
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("bp/manifest.json", bytes(BP_MANIFEST));
        entries.put("bp/scripts/main.js", bytes("import { world } from '@minecraft/server';"));
        entries.put("rp/manifest.json", bytes(RP_MANIFEST));
        entries.put("rp/pack_icon.png", new byte[] {1, 2, 3});
        entries.put("rp/textures/blocks/a.png", bytes("root"));
        entries.put("rp/textures/blocks/b.png", bytes("root b"));
        entries.put("rp/subpacks/plain/textures/blocks/a.png", bytes("plain"));
        entries.put("rp/subpacks/plain/local_lighting/local_lighting.json", bytes("""
            {"format_version": "1.21.120", "minecraft:local_light_settings": {
              "minecraft:torch": {"light_color": "#d4754a", "light_type": "point_light"},
              "minecraft:lit_redstone_lamp": {"light_color": "#975435", "light_type": "static_light"},
              "minecraft:lava": {"light_color": "#ff8000", "light_type": "static_light"},
              "minecraft:flowing_lava": {"light_color": "#000000", "light_type": "static_light"},
              "vazah:light_soul": {"light_color": "#29c2f1", "light_type": "point_light"}}}
            """));
        entries.put("rp/subpacks/fancy/textures/blocks/a.png", bytes("fancy"));
        Path addon = zip("Lights.mcaddon", entries);

        try (BedrockPackFiles files = BedrockPackFiles.open(addon, temp.resolve("scratch"))) {
            assertEquals(1, files.resourcePacks().size());
            assertEquals("plain", files.resourcePacks().get(0).subpack());
            assertEquals("rp/", files.resourcePacks().get(0).where());
            assertEquals("Lights Pack", files.name());
            assertEquals(1, files.skipped().size());
            assertEquals(List.of("data", "script"), files.skipped().get(0).moduleTypes());
            // The pack list says only the resource pack is used, and what was left out.
            JsonObject description = JsonParser.parseString(BedrockPackConverter.packMeta(files.name(),
                files.skipped())).getAsJsonObject().getAsJsonObject("pack");
            var note = description.getAsJsonArray("description").get(1).getAsJsonObject();
            assertEquals(BedrockPackConverter.ADDON_DESCRIPTION_KEY, note.get("translate").getAsString());
            assertEquals("data, script", note.getAsJsonArray("with").get(0).getAsString());
            assertTrue(BedrockPackConverter.packMeta("Plain", List.of()).contains("converted from Bedrock"));

            // The subpack over the pack root, the pack root under it; the behavior pack not at all.
            assertEquals("plain", string(files.read("textures/blocks/a.png")));
            assertEquals("root b", string(files.read("textures/blocks/b.png")));
            assertArrayEquals(new byte[] {1, 2, 3}, files.read("pack_icon.png"));
            assertNull(files.read("scripts/main.js"));
            assertTrue(files.paths().contains("local_lighting/local_lighting.json"));
            assertFalse(files.paths().stream().anyMatch(path -> path.startsWith("subpacks/")));

            byte[] converted = BedrockLocalLighting.convert(files);
            assertNotNull(converted);
            JsonObject blocks = JsonParser.parseString(string(converted)).getAsJsonObject().getAsJsonObject("blocks");
            assertEquals("point_light", blocks.getAsJsonObject("minecraft:torch").get("type").getAsString());
            assertEquals(0xd4, blocks.getAsJsonObject("minecraft:torch").getAsJsonArray("color").get(0).getAsInt());
            // Bedrock's lit block names land on Java's; flowing lava folds into lava, the first entry winning.
            assertTrue(blocks.has("minecraft:redstone_lamp"));
            assertEquals(0xff, blocks.getAsJsonObject("minecraft:lava").getAsJsonArray("color").get(0).getAsInt());
            assertTrue(blocks.has("vazah:light_soul"));
        }
    }

    /** An .mcaddon that holds its packs as .mcpack files, one of them wrapped in a folder of its own. */
    @Test
    void addonWithNestedMcpacks() throws IOException {
        Map<String, byte[]> rp = new LinkedHashMap<>();
        rp.put("Lights RP/manifest.json", bytes(RP_MANIFEST));
        rp.put("Lights RP/textures/blocks/a.png", bytes("nested root"));
        Map<String, byte[]> bp = new LinkedHashMap<>();
        bp.put("manifest.json", bytes(BP_MANIFEST));
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("Lights BP.mcpack", zipBytes(bp));
        entries.put("Lights RP.mcpack", zipBytes(rp));
        Path addon = zip("Nested.mcaddon", entries);

        Path scratch = temp.resolve("scratch");
        try (BedrockPackFiles files = BedrockPackFiles.open(addon, scratch)) {
            assertEquals(1, files.resourcePacks().size());
            assertNull(files.resourcePacks().get(0).subpack(), "no subpack folder is shipped");
            assertEquals(1, files.skipped().size());
            assertEquals("nested root", string(files.read("textures/blocks/a.png")));
            assertNull(BedrockLocalLighting.convert(files));
        }
        try (var left = Files.list(scratch)) {
            assertEquals(0, left.count(), "the nested archives' copies are deleted");
        }
    }

    /** A plain .mcpack without a manifest is still read whole, as before. */
    @Test
    void mcpackWithoutManifest() throws IOException {
        Path pack = zip("Old.mcpack", Map.of("textures/blocks/a.png", bytes("old")));
        try (BedrockPackFiles files = BedrockPackFiles.open(pack, temp.resolve("scratch"))) {
            assertEquals(1, files.resourcePacks().size());
            assertTrue(files.skipped().isEmpty());
            assertNull(files.name());
            assertEquals("old", string(files.read("textures/blocks/a.png")));
        }
    }

    /** An .mcaddon of behavior packs only has nothing to read. */
    @Test
    void behaviorOnly() throws IOException {
        Path addon = zip("Behavior.mcaddon", Map.of("bp/manifest.json", bytes(BP_MANIFEST)));
        try (BedrockPackFiles files = BedrockPackFiles.open(addon, temp.resolve("scratch"))) {
            assertTrue(files.resourcePacks().isEmpty());
            assertEquals(1, files.skipped().size());
            assertEquals("Lights Behavior", files.skipped().get(0).name());
        }
    }

    /** A nested archive that unpacks to far more than it takes (a zip bomb) is left out, and no copy stays behind. */
    @Test
    void nestedZipBombIsRefused() throws IOException {
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("rp/manifest.json", bytes(RP_MANIFEST));
        // 64 MB of zeros deflate to about 64 KB: a ratio near 1000.
        entries.put("bomb.mcpack", new byte[64 * 1024 * 1024]);
        Path addon = zip("Bomb.mcaddon", entries);
        Path scratch = temp.resolve("scratch");
        try (BedrockPackFiles files = BedrockPackFiles.open(addon, scratch)) {
            assertEquals(1, files.resourcePacks().size());
            assertEquals(1, files.warnings().size());
            assertTrue(files.warnings().get(0).contains("zip bomb"), files.warnings().get(0));
        }
        assertFalse(Files.exists(scratch) && Files.list(scratch).findAny().isPresent());
    }

    /** Archives inside a pack's own folder are its files, not packs: they are never unpacked. */
    @Test
    void archivesInsidePackFoldersAreNotFollowed() throws IOException {
        Map<String, byte[]> inner = Map.of("manifest.json", bytes(BP_MANIFEST));
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("rp/manifest.json", bytes(RP_MANIFEST));
        entries.put("rp/extras/inner.mcpack", zipBytes(inner));
        Path addon = zip("Inside.mcaddon", entries);
        Path scratch = temp.resolve("scratch");
        try (BedrockPackFiles files = BedrockPackFiles.open(addon, scratch)) {
            assertEquals(1, files.resourcePacks().size());
            assertTrue(files.skipped().isEmpty());
            assertFalse(Files.exists(scratch));
        }
    }

    /** No more than a handful of nested archives are unpacked from one file; the rest are reported. */
    @Test
    void tooManyNestedArchives() throws IOException {
        Map<String, byte[]> entries = new LinkedHashMap<>();
        for (int i = 0; i < 12; i++) {
            entries.put("pack" + i + ".mcpack", zipBytes(Map.of("manifest.json", bytes(RP_MANIFEST))));
        }
        Path addon = zip("Many.mcaddon", entries);
        try (BedrockPackFiles files = BedrockPackFiles.open(addon, temp.resolve("scratch"))) {
            assertEquals(8, files.resourcePacks().size());
            assertTrue(files.warnings().get(0).contains("too many nested archives"), files.warnings().get(0));
        }
    }

    /**
     * A shield attachable with a torch on each side (Vazah's Dynamic Lights): the torches become an item model over
     * Java's shield, one per hand, and the flame's emissive texels light the texture and the hand.
     */
    @Test
    void torchShield() throws IOException {
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("rp/manifest.json", bytes(RP_MANIFEST));
        entries.put("rp/attachables/shield.entity.json", bytes("""
            {"format_version": "1.10.0", "minecraft:attachable": {"description": {"identifier": "minecraft:shield",
              "materials": {"default": "entity_alphatest", "torch": "entity_emissive"},
              "textures": {"default": "textures/entity/shield_torch"},
              "geometry": {"default": "geometry.shield_torch"},
              "render_controllers": ["controller.render.shield_torch"]}}}
            """));
        entries.put("rp/render_controllers/shield.json", bytes("""
            {"format_version": "1.10", "render_controllers": {"controller.render.shield_torch": {
              "part_visibility": [{"torch_r": "v.main_hand"}, {"torch_l": "v.off_hand"}]}}}
            """));
        entries.put("rp/models/entity/shield.geo.json", bytes("""
            {"format_version": "1.16.0", "minecraft:geometry": [{"description": {"identifier": "geometry.shield_torch",
              "texture_width": 64, "texture_height": 64}, "bones": [
              {"name": "shield", "pivot": [1, 15.5, 3], "cubes": [
                {"origin": [0, 25, 0], "size": [2, 6, 6], "uv": [26, 0]},
                {"origin": [0, 26, 1], "size": [2, 4, 4], "inflate": 0.01, "uv": [29, 18]},
                {"origin": [-5, 17, -1], "size": [12, 22, 1], "uv": [0, 0]}]},
              {"name": "torch_r", "parent": "shield", "cubes": [{"origin": [-6, 36, -1], "size": [1, 5, 1],
                "uv": {"north": {"uv": [26, 13], "uv_size": [2, 10]}}}]},
              {"name": "torch_l", "parent": "shield", "cubes": [{"origin": [7, 36, -1], "size": [1, 5, 1],
                "uv": {"north": {"uv": [26, 13], "uv_size": [2, 10]}}}]}]}]}
            """));
        java.awt.image.BufferedImage texture = new java.awt.image.BufferedImage(64, 64,
            java.awt.image.BufferedImage.TYPE_INT_ARGB);
        texture.setRGB(26, 13, 0x0BFFAA14);
        texture.setRGB(26, 15, 0xFF9F7F50);
        ByteArrayOutputStream png = new ByteArrayOutputStream();
        javax.imageio.ImageIO.write(texture, "png", png);
        entries.put("rp/textures/entity/shield_torch.png", png.toByteArray());
        Path addon = zip("Shield.mcaddon", entries);

        try (BedrockPackFiles files = BedrockPackFiles.open(addon, temp.resolve("scratch"))) {
            BedrockAttachables shield = BedrockAttachables.convert(files);
            Map<String, byte[]> out = shield.files();
            assertTrue(out.containsKey("assets/minecraft/items/shield.json"));
            assertTrue(out.containsKey("assets/minecraft/textures/" + BedrockAttachables.TEXTURE + "_s.png"));
            JsonObject right = JsonParser.parseString(string(out.get(
                "assets/radiante/models/item/bedrock_shield_right.json"))).getAsJsonObject();
            // Only the right torch, at x 6 to 7 above the plate's top edge (y 11), as Java's shield model has it.
            assertEquals(1, right.getAsJsonArray("elements").size());
            JsonObject torch = right.getAsJsonArray("elements").get(0).getAsJsonObject();
            assertEquals(6.0f, torch.getAsJsonArray("from").get(0).getAsFloat());
            assertEquals(8.0f, torch.getAsJsonArray("from").get(1).getAsFloat());
            assertEquals(13.0f, torch.getAsJsonArray("to").get(1).getAsFloat());
            assertTrue(out.containsKey("assets/radiante/models/item/bedrock_shield_left_blocking.json"));
            JsonObject held = shield.held();
            assertNotNull(held);
            assertEquals(255, held.getAsJsonArray("minecraft:shield").get(0).getAsInt());

            JsonObject lights = JsonParser.parseString(string(BedrockLocalLighting.convert(files, held)))
                .getAsJsonObject();
            assertTrue(lights.has("held"));
        }
    }

    @Test
    void cacheNames() {
        assertEquals("Pack.zip", BedrockPackConverter.cacheName("Pack.mcpack"));
        assertEquals("Pack.mcaddon.zip", BedrockPackConverter.cacheName("Pack.MCADDON"));
    }

    private Path zip(String name, Map<String, byte[]> entries) throws IOException {
        Path path = temp.resolve(name);
        try (OutputStream out = Files.newOutputStream(path)) {
            out.write(zipBytes(entries));
        }
        return path;
    }

    private static byte[] zipBytes(Map<String, byte[]> entries) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zos = new ZipOutputStream(bytes)) {
            for (Map.Entry<String, byte[]> entry : entries.entrySet()) {
                zos.putNextEntry(new ZipEntry(entry.getKey()));
                zos.write(entry.getValue());
                zos.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    private static byte[] bytes(String text) {
        return text.getBytes(StandardCharsets.UTF_8);
    }

    private static String string(byte[] data) {
        return new String(data, StandardCharsets.UTF_8);
    }
}
