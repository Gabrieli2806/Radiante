package com.g2806.radiante.mixin.gui;

import net.minecraft.client.MouseHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Where the game takes the cursor to be, set after Photo Mode puts the cursor back where a drag began. */
@Mixin(MouseHandler.class)
public interface MouseHandlerAccessor {

    @Accessor("xpos")
    void radiante$setX(double x);

    @Accessor("ypos")
    void radiante$setY(double y);
}
