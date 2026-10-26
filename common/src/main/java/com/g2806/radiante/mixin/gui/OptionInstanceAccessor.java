package com.g2806.radiante.mixin.gui;

import java.util.function.Function;
import net.minecraft.client.OptionInstance;
import net.minecraft.network.chat.Component;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Mutable;
import org.spongepowered.asm.mixin.gen.Accessor;

/**
 * Lets the Radiante settings screen draw an option its own way (name on the left, value or checkbox on the right, the
 * description beside the list) instead of as a vanilla button.
 */
@Mixin(OptionInstance.class)
public interface OptionInstanceAccessor {

    @Accessor("caption")
    Component radiante$caption();

    /**
     * An addon's option is named by a component the addon supplies, which may be no translation key at all; the
     * option is made with a placeholder key and given its name here.
     */
    @Mutable
    @Accessor("caption")
    void radiante$setCaption(Component caption);

    @Accessor("tooltip")
    OptionInstance.TooltipSupplier<Object> radiante$tooltip();

    @Accessor("toString")
    Function<Object, Component> radiante$toString();
}
