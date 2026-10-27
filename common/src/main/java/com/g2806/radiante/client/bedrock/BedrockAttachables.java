package com.g2806.radiante.client.bedrock;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import javax.imageio.ImageIO;

/**
 * A Bedrock pack's attachable for the shield: the model it draws in the hand instead of the plain shield. Add-ons use
 * it to fix things to the shield, such as the torch of Vazah's Dynamic Lights, glowing through Bedrock's emissive
 * entity material. Java draws its shield with a built in renderer that takes no extra parts, so what the attachable
 * adds to the shield's own plate and handle becomes an item model drawn over it ({@code minecraft:composite}), with
 * the attachable's texture, and the texels Bedrock lights become LabPBR emission. A shield whose attachable glows is
 * also a light in the hand (see {@link #held()}, read by HeldLight through BedrockPointLights).
 *
 * <p>Only the default texture is used: the add-on's variants (an unlit torch in water, a soul torch when one is in the
 * other hand) are picked by Molang at run time, which Java has no counterpart for. Cubes that turn (a rotated bone or
 * cube) are left out. Other items' attachables are not converted: their Java renderers differ item by item.
 */
final class BedrockAttachables {

    /** Where the shield overlay's texture and models go in the converted pack. */
    static final String TEXTURE = "item/radiante_bedrock/shield";
    private static final String MODEL = "radiante:item/bedrock_shield_";

    /** Java 26.3's own shield item definition, drawn under the overlay. */
    private static final String VANILLA_SHIELD = """
        {"type": "minecraft:condition", "property": "minecraft:using_item",
         "on_false": {"type": "minecraft:special", "base": "minecraft:item/shield", "model": {"type": "minecraft:shield"}},
         "on_true": {"type": "minecraft:special", "base": "minecraft:item/shield_blocking",
                     "model": {"type": "minecraft:shield"}},
         "transformation": {"left_rotation": [0, 0, 0, 1], "right_rotation": [0, 0, 0, 1], "scale": [1, -1, -1],
                            "translation": [0, 0, 0]}}
        """;

    /** Size of the Java shield's plate and handle, as Bedrock's shield geometry has them too. */
    private static final float[] PLATE = {12, 22, 1};
    private static final float[] HANDLE = {2, 6, 6};

    /** Which hand a bone is drawn for, from the render controller's part visibility; null for both. */
    private enum Hand { MAIN, OFF }

    private record Cube(float[] origin, float[] size, JsonElement uv, Hand hand) {
    }

    /** The files of the converted shield, by path in the converted pack, or empty when the pack has none. */
    private final Map<String, byte[]> files = new HashMap<>();
    /** The light the shield gives in the hand: its glowing texels' colour (0-255), or null. */
    private int[] heldColour;

    private BedrockAttachables() {
    }

    Map<String, byte[]> files() {
        return files;
    }

    /** {@code {"minecraft:shield": [r, g, b]}} when the shield glows, for the local lighting file; null otherwise. */
    JsonObject held() {
        if (heldColour == null) {
            return null;
        }
        JsonObject held = new JsonObject();
        JsonArray colour = new JsonArray();
        colour.add(heldColour[0]);
        colour.add(heldColour[1]);
        colour.add(heldColour[2]);
        held.add("minecraft:shield", colour);
        return held;
    }

    /** Converts the pack's shield attachable, if it has one that adds parts to the shield. */
    static BedrockAttachables convert(BedrockPackFiles pack) throws IOException {
        BedrockAttachables out = new BedrockAttachables();
        JsonObject attachable = null;
        Map<String, JsonObject> geometries = new HashMap<>();
        List<JsonObject> controllers = new ArrayList<>();
        for (String path : pack.paths()) {
            if (!path.endsWith(".json")) {
                continue;
            }
            try {
                if (path.startsWith("attachables/")) {
                    JsonObject description = json(pack, path).getAsJsonObject("minecraft:attachable")
                        .getAsJsonObject("description");
                    if ("minecraft:shield".equals(description.get("identifier").getAsString())) {
                        attachable = description;
                    }
                } else if (path.startsWith("models/")) {
                    JsonElement list = json(pack, path).get("minecraft:geometry");
                    if (list != null && list.isJsonArray()) {
                        for (JsonElement geometry : list.getAsJsonArray()) {
                            String id = geometry.getAsJsonObject().getAsJsonObject("description").get("identifier")
                                .getAsString();
                            geometries.put(id, geometry.getAsJsonObject());
                        }
                    }
                } else if (path.startsWith("render_controllers/")) {
                    controllers.add(json(pack, path).getAsJsonObject("render_controllers"));
                }
            } catch (RuntimeException e) {
                // One malformed file only loses what it held.
            }
        }
        if (attachable == null || !attachable.has("geometry") || !attachable.has("textures")) {
            return out;
        }
        JsonObject geometry = geometries.get(attachable.getAsJsonObject("geometry").get("default").getAsString());
        JsonElement texturePath = attachable.getAsJsonObject("textures").get("default");
        if (geometry == null || texturePath == null) {
            return out;
        }
        TgaReader.Image texture = image(pack, texturePath.getAsString());
        if (texture == null) {
            return out;
        }
        Map<String, Hand> hands = new HashMap<>();
        for (JsonObject group : controllers) {
            for (String name : attachableControllers(attachable)) {
                if (group.has(name)) {
                    readVisibility(group.getAsJsonObject(name), hands);
                }
            }
        }
        out.build(geometry, texture, hands, hasEmissiveMaterial(attachable));
        return out;
    }

    private void build(JsonObject geometry, TgaReader.Image texture, Map<String, Hand> hands, boolean emissive)
        throws IOException {
        float[] plate = null;
        List<Cube> extra = new ArrayList<>();
        Map<String, JsonObject> bones = new HashMap<>();
        for (JsonElement element : geometry.getAsJsonArray("bones")) {
            JsonObject bone = element.getAsJsonObject();
            bones.put(bone.get("name").getAsString(), bone);
        }
        for (JsonObject bone : bones.values()) {
            if (!bone.has("cubes") || turns(bone)) {
                continue;
            }
            Hand hand = handOf(bone, bones, hands);
            for (JsonElement element : bone.getAsJsonArray("cubes")) {
                JsonObject cube = element.getAsJsonObject();
                if (turns(cube) || !cube.has("origin") || !cube.has("size")) {
                    continue;
                }
                float[] origin = vec(cube.getAsJsonArray("origin"));
                float[] size = vec(cube.getAsJsonArray("size"));
                boolean inflated = cube.has("inflate") && cube.get("inflate").getAsFloat() != 0.0f;
                if (java.util.Arrays.equals(size, PLATE) && !inflated) {
                    plate = origin;
                } else if (!java.util.Arrays.equals(size, HANDLE) && !inside(origin, size, bone)) {
                    extra.add(new Cube(origin, size, cube.get("uv"), hand));
                }
            }
        }
        if (plate == null || extra.isEmpty()) {
            return;
        }

        // The parts the overlay draws, per hand; the shield's own geometry draws the plate and the handle.
        boolean split = extra.stream().anyMatch(cube -> cube.hand() != null);
        for (String side : split ? List.of("right", "left") : List.of("both")) {
            Hand only = side.equals("right") ? Hand.MAIN : side.equals("left") ? Hand.OFF : null;
            JsonArray elements = new JsonArray();
            for (Cube cube : extra) {
                if (only == null || cube.hand() == null || cube.hand() == only) {
                    elements.add(element(cube, plate, texture.width(), texture.height()));
                }
            }
            for (String state : List.of("", "_blocking")) {
                JsonObject model = new JsonObject();
                model.addProperty("parent", "minecraft:item/shield" + state);
                JsonObject textures = new JsonObject();
                textures.addProperty("layer", "minecraft:" + TEXTURE);
                textures.addProperty("particle", "minecraft:" + TEXTURE);
                model.add("textures", textures);
                model.add("elements", elements);
                files.put("assets/radiante/models/item/bedrock_shield_" + side + state + ".json",
                    model.toString().getBytes(StandardCharsets.UTF_8));
            }
        }
        files.put("assets/minecraft/items/shield.json", itemDefinition(split).getBytes(StandardCharsets.UTF_8));

        // Bedrock's emissive entity material lights a texel by how far its alpha is below full; the texel itself is
        // drawn opaque. Java would cut those texels out, so the copy is made opaque and the light goes to LabPBR.
        int[] colour = texture.argb().clone();
        int[] specular = new int[colour.length];
        long r = 0, g = 0, b = 0, glowing = 0;
        for (int i = 0; i < colour.length; i++) {
            int alpha = colour[i] >>> 24;
            int emission = emissive && alpha > 0 && alpha < 255 ? 255 - alpha : 0;
            if (emission > 0) {
                colour[i] |= 0xFF000000;
                r += colour[i] >> 16 & 0xFF;
                g += colour[i] >> 8 & 0xFF;
                b += colour[i] & 0xFF;
                glowing++;
            }
            specular[i] = BedrockPackConverter.labPbrSpecular(0, emission / 4, 255, 0);
        }
        TgaReader.Image opaque = new TgaReader.Image(texture.width(), texture.height(), colour);
        files.put("assets/minecraft/textures/" + TEXTURE + ".png", BedrockPackConverter.png(opaque));
        // The shield itself takes the attachable's look too: Java draws the plate and handle from this texture.
        files.put("assets/minecraft/textures/entity/shield/shield_base_nopattern.png", BedrockPackConverter.png(opaque));
        if (glowing > 0) {
            files.put("assets/minecraft/textures/" + TEXTURE + "_s.png",
                BedrockPackConverter.png(new TgaReader.Image(texture.width(), texture.height(), specular)));
            heldColour = new int[] {(int) (r / glowing), (int) (g / glowing), (int) (b / glowing)};
        }
    }

    /**
     * The item definition: Java's shield, with the overlay over it, the right-hand parts in the right hand (and
     * anywhere else: the inventory, the ground), the left-hand ones in the left, each in the blocking pose while the
     * shield is raised.
     */
    private static String itemDefinition(boolean split) {
        String overlay;
        if (split) {
            overlay = "{\"type\": \"minecraft:condition\", \"property\": \"minecraft:using_item\", "
                + "\"on_false\": " + bySide("") + ", \"on_true\": " + bySide("_blocking") + "}";
        } else {
            overlay = "{\"type\": \"minecraft:condition\", \"property\": \"minecraft:using_item\", "
                + "\"on_false\": {\"type\": \"minecraft:model\", \"model\": \"" + MODEL + "both\"}, "
                + "\"on_true\": {\"type\": \"minecraft:model\", \"model\": \"" + MODEL + "both_blocking\"}}";
        }
        return "{\"model\": {\"type\": \"minecraft:composite\", \"models\": [" + VANILLA_SHIELD + ", " + overlay
            + "]}}";
    }

    private static String bySide(String state) {
        return "{\"type\": \"minecraft:select\", \"property\": \"minecraft:display_context\", \"cases\": [{\"when\": "
            + "[\"firstperson_lefthand\", \"thirdperson_lefthand\"], \"model\": {\"type\": \"minecraft:model\", "
            + "\"model\": \"" + MODEL + "left" + state + "\"}}], \"fallback\": {\"type\": \"minecraft:model\", "
            + "\"model\": \"" + MODEL + "right" + state + "\"}}";
    }

    /**
     * One cube as an item model element. Bedrock's shield geometry and the item model Java draws the shield in are
     * mirrored in x and z; the plate gives the offset, as it sits at x -6 to 6, y -11 to 11 and z 1 to 2 there.
     */
    private static JsonObject element(Cube cube, float[] plate, int width, int height) {
        float[] o = cube.origin();
        float[] s = cube.size();
        float x0 = (plate[0] + PLATE[0] - 6.0f) - (o[0] + s[0]);
        float y0 = o[1] - plate[1] - 11.0f;
        float z0 = (plate[2] + PLATE[2] + 1.0f) - (o[2] + s[2]);
        JsonObject element = new JsonObject();
        element.add("from", array(x0, y0, z0));
        element.add("to", array(x0 + s[0], y0 + s[1], z0 + s[2]));
        JsonObject faces = new JsonObject();
        Map<String, float[]> uvs = faceUvs(cube.uv(), s);
        // Mirrored in x and z: Bedrock's east face is the model's west, its north the model's south.
        Map<String, String> javaFace = Map.of("north", "south", "south", "north", "east", "west", "west", "east",
            "up", "up", "down", "down");
        for (Map.Entry<String, float[]> entry : uvs.entrySet()) {
            float[] uv = entry.getValue();
            JsonObject face = new JsonObject();
            face.add("uv", array(uv[0] * 16.0f / width, uv[1] * 16.0f / height, (uv[0] + uv[2]) * 16.0f / width,
                (uv[1] + uv[3]) * 16.0f / height));
            face.addProperty("texture", "#layer");
            faces.add(javaFace.get(entry.getKey()), face);
        }
        element.add("faces", faces);
        return element;
    }

    /** Each face's texture rectangle (u, v, width, height in texels), per face or from Bedrock's box layout. */
    private static Map<String, float[]> faceUvs(JsonElement uv, float[] size) {
        Map<String, float[]> faces = new HashMap<>();
        if (uv == null) {
            return faces;
        }
        if (uv.isJsonObject()) {
            for (Map.Entry<String, JsonElement> entry : uv.getAsJsonObject().entrySet()) {
                JsonObject face = entry.getValue().getAsJsonObject();
                float[] at = vec(face.getAsJsonArray("uv"));
                float[] extent = face.has("uv_size") ? vec(face.getAsJsonArray("uv_size")) : new float[] {1, 1};
                faces.put(entry.getKey().toLowerCase(Locale.ROOT), new float[] {at[0], at[1], extent[0], extent[1]});
            }
            return faces;
        }
        float[] at = vec(uv.getAsJsonArray());
        float w = size[0], h = size[1], d = size[2];
        float u = at[0], v = at[1];
        faces.put("up", new float[] {u + d, v, w, d});
        faces.put("down", new float[] {u + d + w, v + d, w, -d});
        faces.put("west", new float[] {u, v + d, d, h});
        faces.put("north", new float[] {u + d, v + d, w, h});
        faces.put("east", new float[] {u + d + w, v + d, d, h});
        faces.put("south", new float[] {u + d + w + d, v + d, w, h});
        return faces;
    }

    /** A detail cube of the shield's own (the handle's grip), inside the bone's handle: drawn by Java already. */
    private static boolean inside(float[] origin, float[] size, JsonObject bone) {
        for (JsonElement element : bone.getAsJsonArray("cubes")) {
            JsonObject cube = element.getAsJsonObject();
            float[] o = vec(cube.getAsJsonArray("origin"));
            float[] s = vec(cube.getAsJsonArray("size"));
            if (java.util.Arrays.equals(s, HANDLE) && origin[0] >= o[0] && origin[1] >= o[1] && origin[2] >= o[2]
                && origin[0] + size[0] <= o[0] + s[0] && origin[1] + size[1] <= o[1] + s[1]
                && origin[2] + size[2] <= o[2] + s[2]) {
                return true;
            }
        }
        return false;
    }

    private static boolean turns(JsonObject part) {
        if (!part.has("rotation")) {
            return false;
        }
        for (float angle : vec(part.getAsJsonArray("rotation"))) {
            if (angle != 0.0f) {
                return true;
            }
        }
        return false;
    }

    /** A bone's hand: its own part visibility, else its parent's. */
    private static Hand handOf(JsonObject bone, Map<String, JsonObject> bones, Map<String, Hand> hands) {
        for (int depth = 0; bone != null && depth < 32; depth++) {
            Hand hand = hands.get(bone.get("name").getAsString());
            if (hand != null) {
                return hand;
            }
            bone = bone.has("parent") ? bones.get(bone.get("parent").getAsString()) : null;
        }
        return null;
    }

    /** Bones shown only in the main or the off hand ({@code "torch_r": "v.main_hand"}). */
    private static void readVisibility(JsonObject controller, Map<String, Hand> hands) {
        if (!controller.has("part_visibility")) {
            return;
        }
        for (JsonElement element : controller.getAsJsonArray("part_visibility")) {
            for (Map.Entry<String, JsonElement> entry : element.getAsJsonObject().entrySet()) {
                if (!entry.getValue().isJsonPrimitive()) {
                    continue;
                }
                String rule = entry.getValue().getAsString().replace(" ", "").toLowerCase(Locale.ROOT);
                if (rule.equals("v.main_hand") || rule.equals("variable.main_hand")) {
                    hands.put(entry.getKey(), Hand.MAIN);
                } else if (rule.equals("v.off_hand") || rule.equals("variable.off_hand")) {
                    hands.put(entry.getKey(), Hand.OFF);
                }
            }
        }
    }

    private static List<String> attachableControllers(JsonObject attachable) {
        List<String> names = new ArrayList<>();
        if (attachable.has("render_controllers")) {
            for (JsonElement element : attachable.getAsJsonArray("render_controllers")) {
                if (element.isJsonPrimitive()) {
                    names.add(element.getAsString());
                }
            }
        }
        return names;
    }

    /** Whether one of the attachable's materials is one of Bedrock's emissive entity materials. */
    private static boolean hasEmissiveMaterial(JsonObject attachable) {
        if (!attachable.has("materials")) {
            return false;
        }
        for (Map.Entry<String, JsonElement> entry : attachable.getAsJsonObject("materials").entrySet()) {
            if (entry.getValue().isJsonPrimitive() && entry.getValue().getAsString().contains("emissive")) {
                return true;
            }
        }
        return false;
    }

    private static TgaReader.Image image(BedrockPackFiles pack, String path) throws IOException {
        byte[] tga = pack.read(path + ".tga");
        if (tga != null) {
            return TgaReader.read(tga);
        }
        byte[] png = pack.read(path + ".png");
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

    private static JsonObject json(BedrockPackFiles pack, String path) {
        try {
            // Bedrock tolerates comments in its JSON; Gson's lenient parser does too.
            return JsonParser.parseString(new String(pack.read(path), StandardCharsets.UTF_8)).getAsJsonObject();
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
    }

    private static float[] vec(JsonArray array) {
        float[] values = new float[array.size()];
        for (int i = 0; i < values.length; i++) {
            values[i] = array.get(i).getAsFloat();
        }
        return values;
    }

    private static JsonArray array(float... values) {
        JsonArray array = new JsonArray();
        for (float value : values) {
            array.add(Math.round(value * 1000.0f) / 1000.0f);
        }
        return array;
    }
}
