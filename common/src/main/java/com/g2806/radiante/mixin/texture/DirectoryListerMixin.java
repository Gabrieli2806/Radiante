package com.g2806.radiante.mixin.texture;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import java.util.LinkedHashMap;
import java.util.Map;
import net.minecraft.client.renderer.texture.atlas.sources.DirectoryLister;
import net.minecraft.resources.FileToIdConverter;
import net.minecraft.resources.Identifier;
import net.minecraft.server.packs.resources.Resource;
import net.minecraft.server.packs.resources.ResourceManager;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * Keeps a pack's PBR maps out of the atlases as sprites of their own. An atlas built from a directory takes every PNG
 * in it, "stone_n.png" and "stone_s.png" next to "stone.png" included. Nothing draws those sprites, and they are two
 * in three of a PBR pack's textures: with a Bedrock RTX pack converted at 96 pixels a block the block atlas came out
 * 8192 x 8192 instead of 8192 x 4096, and with it each of the three maps stitched to match it (PbrAtlases) - half of
 * well over a gigabyte of video memory spent on copies nobody reads.
 *
 * <p>Only a file named after another texture of the same directory with "_n" or "_s" added is left out; the maps are
 * read from the pack itself, not from the atlas.
 */
@Mixin(DirectoryLister.class)
public abstract class DirectoryListerMixin {

    @WrapOperation(method = "run", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/resources/FileToIdConverter;listMatchingResources(Lnet/minecraft/server/packs/resources/ResourceManager;)Ljava/util/Map;"))
    private Map<Identifier, Resource> radiante$withoutPbrMaps(FileToIdConverter lister, ResourceManager resources,
        Operation<Map<Identifier, Resource>> original) {
        Map<Identifier, Resource> found = original.call(lister, resources);
        Map<Identifier, Resource> kept = null;
        for (Identifier file : found.keySet()) {
            String path = file.getPath();
            if (!path.endsWith("_n.png") && !path.endsWith("_s.png")) {
                continue;
            }
            Identifier base = Identifier.fromNamespaceAndPath(file.getNamespace(),
                path.substring(0, path.length() - "_n.png".length()) + ".png");
            if (!found.containsKey(base)) {
                continue;
            }
            if (kept == null) {
                kept = new LinkedHashMap<>(found);
            }
            kept.remove(file);
        }
        return kept == null ? found : kept;
    }
}
