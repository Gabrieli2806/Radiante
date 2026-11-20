package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.option.EmissionOverrides;
import com.g2806.radiante.client.option.EmissionColor;
import com.g2806.radiante.client.render.BlockEmissionControls;
import com.g2806.radiante.mixin.gui.OptionInstanceAccessor;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import net.minecraft.client.OptionInstance;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;

/**
 * The Block Emission group: one row per light block, its textures under one colour. Each texture of a block was a row
 * of its own (a crafter's east, south and top, a command block's four faces, a furnace's lit front), named by its
 * face, so one block took several rows that set the same light piece by piece. A texture no block owns keeps its own
 * row. Each row shows the block's texture, and its editor a preview of the light.
 */
final class BlockEmissionSettings {
    static final String PREFIX = "options.radiante.emission_profile.";
    /** The textures each row stands for, the first one drawn as its icon where there is no item (drawIcon). */
    static final Map<OptionInstance<?>, List<String>> ROW_SPRITES = new java.util.IdentityHashMap<>();
    /** The item each row's block is in the inventory (a wall torch's torch, redstone wire's redstone), or none. */
    static final Map<OptionInstance<?>, net.minecraft.world.item.Item> ROW_ITEMS = new java.util.IdentityHashMap<>();
    private static final Map<net.minecraft.world.item.Item, net.minecraft.world.item.ItemStack> STACKS =
        new java.util.IdentityHashMap<>();

    private BlockEmissionSettings() {
    }

    static SettingsLayout.Group group(Map<String, EmissionColor> staged) {
        Map<String, List<String>> byBlock = new java.util.TreeMap<>();
        for (String id : BlockEmissionControls.profiles()) {
            byBlock.computeIfAbsent(blockKey(id), k -> new ArrayList<>()).add(id);
        }
        List<Map.Entry<String, List<String>>> blocks = new ArrayList<>(byBlock.entrySet());
        blocks.sort(Comparator.comparing(entry -> blockTitle(entry.getKey(), entry.getValue()).getString()));
        ROW_SPRITES.clear();
        ROW_ITEMS.clear();
        List<OptionInstance<?>> rows = new ArrayList<>();
        for (Map.Entry<String, List<String>> block : blocks) {
            List<String> ids = List.copyOf(block.getValue());
            String key = PREFIX + block.getKey();
            Component name = blockTitle(block.getKey(), ids);
            EmissionColor current = EmissionColor.WHITE;
            for (String id : ids) {
                EmissionColor colour = EmissionOverrides.color(staged, id);
                if (!colour.equals(EmissionColor.WHITE)) {
                    current = colour;
                    break;
                }
            }
            var row = new OptionInstance<EmissionColor>(key,
                OptionInstance.cachedConstantTooltip(Component.translatable(PREFIX + "tooltip", name)),
                (caption, colour) -> Component.empty().append(caption).append(": " + colour.display()),
                new ColourValues(), current, value -> {
                    for (String id : ids) {
                        if (value.equals(EmissionColor.WHITE)) staged.remove(id);
                        else staged.put(id, value);
                    }
                });
            // A unique key retains focus when captions share a block name; the fallback follows Minecraft's language.
            ((OptionInstanceAccessor) (Object) row).radiante$setCaption(
                Component.translatableWithFallback(key, name.getString()));
            ROW_SPRITES.put(row, ids);
            net.minecraft.world.item.Item item = itemOf(block.getKey(), ids.get(0));
            if (item != null) ROW_ITEMS.put(row, item);
            rows.add(row);
        }
        return new SettingsLayout.Group("options.radiante.advanced.group.block_emission", rows);
    }

    @SuppressWarnings("unchecked")
    static void open(OptionInstance<?> option) {
        var minecraft = net.minecraft.client.Minecraft.getInstance();
        var colour = (OptionInstance<EmissionColor>) option;
        minecraft.gui.setScreen(new HdrEmissionScreen(minecraft.gui.screen(),
            ((OptionInstanceAccessor) (Object) option).radiante$caption(), colour.get(), colour::set,
            ROW_SPRITES.getOrDefault(option, List.of()), ROW_ITEMS.get(option)));
    }

    /** The item a row stands for: its block's, or the item a texture is named for (the glow item frame). */
    private static net.minecraft.world.item.Item itemOf(String blockKey, String sprite) {
        Identifier blockId = Identifier.tryParse(blockKey);
        if (blockId != null && BuiltInRegistries.BLOCK.containsKey(blockId)) {
            net.minecraft.world.item.Item item = BuiltInRegistries.BLOCK.getValue(blockId).asItem();
            if (item != net.minecraft.world.item.Items.AIR) return item;
        }
        Identifier name = Identifier.tryParse(sprite);
        if (name == null) return null;
        Identifier itemId = Identifier.fromNamespaceAndPath(name.getNamespace(), name.getPath().replaceFirst("^block/", ""));
        return BuiltInRegistries.ITEM.containsKey(itemId) ? BuiltInRegistries.ITEM.getValue(itemId) : null;
    }

    /**
     * Whether the rows' icons are drawn: only once a world has bound the items' components. Before that (the settings
     * opened from the title screen) no item can be made, and a flat texture in its place looked unlike the game.
     */
    static boolean iconsShown() {
        return net.minecraft.world.item.Items.TORCH.builtInRegistryHolder().areComponentsBound();
    }

    /**
     * A row's icon at size (16 is an inventory slot's): its item as the inventory and the creative menu draw it, a
     * lantern as a lantern and a crafter as a block; its texture for a light with no item (fire, lava). Nothing
     * outside a world (iconsShown).
     */
    static void drawIcon(net.minecraft.client.gui.GuiGraphicsExtractor g, net.minecraft.world.item.Item item,
        List<String> sprites, int x, int y, int size) {
        if (!iconsShown()) return;
        if (item != null) {
            net.minecraft.world.item.ItemStack stack = STACKS.computeIfAbsent(item, net.minecraft.world.item.ItemStack::new);
            if (size == 16) {
                g.fakeItem(stack, x, y);
            } else {
                g.pose().pushMatrix();
                g.pose().translate(x, y);
                g.pose().scale(size / 16.0f, size / 16.0f);
                g.fakeItem(stack, 0, 0);
                g.pose().popMatrix();
            }
            return;
        }
        var sprite = sprites == null || sprites.isEmpty() ? null : sprite(sprites.get(0));
        if (sprite != null) {
            com.g2806.radiante.compat.VersionCompat.blitAtlasSprite(g, sprite, x, y, size, size, 0xFFFFFFFF);
        }
    }

    /** The texture id as a block atlas sprite, or null before the atlas is there. */
    static net.minecraft.client.renderer.texture.TextureAtlasSprite sprite(String id) {
        Identifier name = Identifier.tryParse(id);
        var minecraft = net.minecraft.client.Minecraft.getInstance();
        if (name == null || minecraft.getAtlasManager() == null) return null;
        try {
            return minecraft.getAtlasManager().getAtlasOrThrow(net.minecraft.data.AtlasIds.BLOCKS).getSprite(name);
        } catch (RuntimeException missing) {
            return null;
        }
    }

    /** The block a texture belongs to ("minecraft:crafter" for "minecraft:block/crafter_east"), or the texture. */
    private static String blockKey(String name) {
        Identifier sprite = Identifier.tryParse(name);
        if (sprite == null) return name;
        String blockPath = blockPath(sprite);
        while (!blockPath.isEmpty()) {
            Identifier blockId = Identifier.fromNamespaceAndPath(sprite.getNamespace(), blockPath);
            if (BuiltInRegistries.BLOCK.containsKey(blockId)) return blockId.toString();
            int split = blockPath.lastIndexOf('_');
            if (split < 0) break;
            blockPath = blockPath.substring(0, split);
        }
        return name;
    }

    private static Component blockTitle(String blockKey, List<String> ids) {
        Identifier blockId = Identifier.tryParse(blockKey);
        if (blockId != null && BuiltInRegistries.BLOCK.containsKey(blockId)) {
            return BuiltInRegistries.BLOCK.getValue(blockId).getName();
        }
        return title(ids.get(0));
    }

    private static String blockPath(Identifier sprite) {
        String path = sprite.getPath().replaceFirst("^block/", "");
        return switch (path) {
            case "magma" -> "magma_block";
            case "pumpkin_face_on" -> "jack_o_lantern";
            // Redstone dust's sprites, by power (RedstoneGlow's redstone_1 to redstone_15 among them).
            default -> path.startsWith("redstone_dust") || path.matches("redstone_\\d+") ? "redstone_wire" : path;
        };
    }

    private static final class ColourValues implements OptionInstance.ValueSet<EmissionColor> {
        @Override
        public java.util.Optional<EmissionColor> validateValue(EmissionColor value) {
            return java.util.Optional.of(value);
        }

        @Override
        public com.mojang.serialization.Codec<EmissionColor> codec() {
            return com.mojang.serialization.Codec.FLOAT.listOf().comapFlatMap(rgb -> rgb.size() == 3
                ? com.mojang.serialization.DataResult.success(new EmissionColor(rgb.get(0), rgb.get(1), rgb.get(2)))
                : com.mojang.serialization.DataResult.error(() -> "Expected three RGB channels"),
                colour -> List.of(colour.red(), colour.green(), colour.blue()));
        }

        @Override
        public OptionInstance.ButtonFactory<EmissionColor> buttonFactory() {
            return (option, tooltip, options, x, y, width, changed) -> net.minecraft.client.gui.components.Button
                .builder(Component.literal(option.get().display()), button -> open(option)).bounds(x, y, width, 20).build();
        }
    }

    private static Component title(String name) {
        Identifier sprite = Identifier.tryParse(name);
        if (sprite == null) return Component.literal(name);
        String path = sprite.getPath().replaceFirst("^block/", "");
        String blockPath = blockPath(sprite);
        while (!blockPath.isEmpty()) {
            Identifier blockId = Identifier.fromNamespaceAndPath(sprite.getNamespace(), blockPath);
            if (BuiltInRegistries.BLOCK.containsKey(blockId)) {
                var block = BuiltInRegistries.BLOCK.getValue(blockId);
                Component title = block.getName();
                return path.equals(blockPath) ? title :
                    Component.translatable(PREFIX + "variant", title, path);
            }
            int split = blockPath.lastIndexOf('_');
            if (split < 0) break;
            blockPath = blockPath.substring(0, split);
        }
        // Not a block's: an item's (the glow item frame), or the texture's own name, written out.
        Identifier itemId = Identifier.fromNamespaceAndPath(sprite.getNamespace(), path);
        // By its translation key: an item's name through an ItemStack needs its components bound, which they are not
        // before a world is loaded, and the settings opened from the title screen failed to open.
        if (BuiltInRegistries.ITEM.containsKey(itemId)) {
            return Component.translatable(BuiltInRegistries.ITEM.getValue(itemId).getDescriptionId());
        }
        String words = path.replace('/', ' ').replace('_', ' ');
        return Component.literal(words.isEmpty() ? name
            : Character.toUpperCase(words.charAt(0)) + words.substring(1));
    }
}
