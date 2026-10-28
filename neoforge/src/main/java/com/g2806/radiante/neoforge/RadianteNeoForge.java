package com.g2806.radiante.neoforge;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.RadianteKeys;
import com.g2806.radiante.client.command.RadianteCommands;
import com.g2806.radiante.client.gui.RadianteOptionsScreen;
import net.minecraft.client.KeyMapping;
import net.minecraft.commands.CommandSourceStack;
import net.minecraft.client.Minecraft;
import net.neoforged.api.distmarker.Dist;
import net.neoforged.bus.api.IEventBus;
import net.neoforged.fml.ModContainer;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.client.event.ClientTickEvent;
import net.neoforged.neoforge.client.event.RegisterClientCommandsEvent;
import net.neoforged.neoforge.client.event.RegisterKeyMappingsEvent;
import net.neoforged.neoforge.client.gui.IConfigScreenFactory;
import net.neoforged.neoforge.common.NeoForge;

@Mod(value = "radiante", dist = Dist.CLIENT)
public final class RadianteNeoForge {

    public RadianteNeoForge(IEventBus modBus, ModContainer container) {
        RadianteClient.init();

        modBus.addListener(RegisterKeyMappingsEvent.class, event -> {
            for (KeyMapping key : RadianteKeys.ALL) {
                event.register(key);
            }
        });
        NeoForge.EVENT_BUS.addListener(ClientTickEvent.Post.class,
            event -> RadianteClient.onEndClientTick(Minecraft.getInstance()));
        NeoForge.EVENT_BUS.addListener(RegisterClientCommandsEvent.class,
            event -> event.getDispatcher().register(RadianteCommands.<CommandSourceStack>build()));
        container.registerExtensionPoint(IConfigScreenFactory.class,
            (modContainer, parent) -> new RadianteOptionsScreen(parent, Minecraft.getInstance().options));
    }
}
