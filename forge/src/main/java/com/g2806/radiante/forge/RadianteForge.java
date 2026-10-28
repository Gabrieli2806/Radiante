package com.g2806.radiante.forge;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.RadianteKeys;
import com.g2806.radiante.client.command.RadianteCommands;
import com.g2806.radiante.client.gui.RadianteOptionsScreen;
import net.minecraft.client.KeyMapping;
import net.minecraft.commands.CommandSourceStack;
import net.minecraftforge.client.event.RegisterClientCommandsEvent;
import net.minecraftforge.client.ConfigScreenHandler;
import net.minecraftforge.client.event.RegisterKeyMappingsEvent;
import net.minecraftforge.event.TickEvent;
import net.minecraftforge.fml.common.Mod;
import net.minecraftforge.fml.javafmlmod.FMLJavaModLoadingContext;

/** Client only (clientSideOnly in mods.toml). */
@Mod("radiante")
public final class RadianteForge {

    public RadianteForge(FMLJavaModLoadingContext context) {
        RadianteClient.init();

        RegisterKeyMappingsEvent.BUS.addListener(event -> {
            for (KeyMapping key : RadianteKeys.ALL) {
                event.register(key);
            }
        });
        TickEvent.ClientTickEvent.Post.BUS.addListener(
            event -> RadianteClient.onEndClientTick(net.minecraft.client.Minecraft.getInstance()));
        RegisterClientCommandsEvent.BUS.addListener(
            event -> event.getDispatcher().register(RadianteCommands.<CommandSourceStack>build()));
        context.registerExtensionPoint(ConfigScreenHandler.ConfigScreenFactory.class,
            () -> new ConfigScreenHandler.ConfigScreenFactory(
                (minecraft, parent) -> new RadianteOptionsScreen(parent, minecraft.options)));
    }
}
