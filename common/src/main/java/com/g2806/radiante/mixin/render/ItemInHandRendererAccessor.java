package com.g2806.radiante.mixin.render;

import net.minecraft.client.renderer.ItemInHandRenderer;
import net.minecraft.world.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/**
 * 26.2 port: the stacks the first-person hands are drawn with, which 26.3 keeps in its hands render state. During an
 * equip animation they are still the previous stacks, lowering out of view.
 */
@Mixin(ItemInHandRenderer.class)
public interface ItemInHandRendererAccessor {

    @Accessor("mainHandItem")
    ItemStack radiante$mainHandItem();

    @Accessor("offHandItem")
    ItemStack radiante$offHandItem();
}
