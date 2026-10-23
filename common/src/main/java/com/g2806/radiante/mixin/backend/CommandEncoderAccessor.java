package com.g2806.radiante.mixin.backend;

import com.mojang.blaze3d.systems.CommandEncoder;
import com.mojang.blaze3d.systems.CommandEncoderBackend;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** 26.2 port: 26.3's FrontendCommandEncoder.backend() is public; 26.2's CommandEncoder keeps it protected. */
@Mixin(CommandEncoder.class)
public interface CommandEncoderAccessor {

    @Accessor("backend")
    CommandEncoderBackend radiante$backend();
}
