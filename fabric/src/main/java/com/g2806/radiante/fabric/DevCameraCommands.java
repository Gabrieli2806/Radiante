package com.g2806.radiante.fabric;

import com.g2806.radiante.client.DevCameraMarks;
import com.mojang.brigadier.arguments.DoubleArgumentType;
import com.mojang.brigadier.arguments.LongArgumentType;
import com.mojang.brigadier.arguments.StringArgumentType;
import net.fabricmc.fabric.api.client.command.v2.ClientCommands;
import net.fabricmc.fabric.api.client.command.v2.ClientCommandRegistrationCallback;
import net.fabricmc.fabric.api.client.command.v2.FabricClientCommandSource;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.Minecraft;
import net.minecraft.network.chat.Component;

/**
 * Development only (dev environment): {@code /campath} records and plays camera paths from inside the game.
 * Fly to where a shot starts and {@code /campath start}, optionally {@code /campath mark} on the way, fly to where it
 * ends and {@code /campath end <name> [seconds]}; {@code /campath time <ticks>} sets the time of day the shot is taken
 * at, {@code /campath play <name>} shows it, {@code /campath where} prints the coordinates.
 */
final class DevCameraCommands {

    private DevCameraCommands() {
    }

    static void init() {
        if (!FabricLoader.getInstance().isDevelopmentEnvironment()) {
            return;
        }
        ClientCommandRegistrationCallback.EVENT.register((dispatcher, registryAccess) -> dispatcher.register(
            ClientCommands.literal("campath")
                .then(ClientCommands.literal("where").executes(context -> say(context.getSource(),
                    DevCameraMarks.where(Minecraft.getInstance()))))
                .then(ClientCommands.literal("time")
                    .then(ClientCommands.argument("ticks", LongArgumentType.longArg(0L, 24000L))
                        .executes(context -> say(context.getSource(),
                            DevCameraMarks.time(LongArgumentType.getLong(context, "ticks"))))))
                .then(ClientCommands.literal("start").executes(context -> say(context.getSource(),
                    DevCameraMarks.start(Minecraft.getInstance()))))
                .then(ClientCommands.literal("mark").executes(context -> say(context.getSource(),
                    DevCameraMarks.mark(Minecraft.getInstance()))))
                .then(ClientCommands.literal("end")
                    .then(ClientCommands.argument("name", StringArgumentType.word())
                        .executes(context -> say(context.getSource(), DevCameraMarks.end(Minecraft.getInstance(),
                            StringArgumentType.getString(context, "name"), 9.0)))
                        .then(ClientCommands.argument("seconds", DoubleArgumentType.doubleArg(1.0, 120.0))
                            .executes(context -> say(context.getSource(), DevCameraMarks.end(Minecraft.getInstance(),
                                StringArgumentType.getString(context, "name"),
                                DoubleArgumentType.getDouble(context, "seconds")))))))
                .then(ClientCommands.literal("play")
                    .then(ClientCommands.argument("name", StringArgumentType.word())
                        .executes(context -> say(context.getSource(), DevCameraMarks.play(Minecraft.getInstance(),
                            StringArgumentType.getString(context, "name"))))))
                .then(ClientCommands.literal("stop").executes(context -> say(context.getSource(),
                    DevCameraMarks.stop())))
                .then(ClientCommands.literal("list").executes(context -> say(context.getSource(),
                    String.join(", ", DevCameraMarks.saved(Minecraft.getInstance())))))));
    }

    private static int say(FabricClientCommandSource source, String text) {
        source.sendFeedback(Component.literal(text));
        return 1;
    }
}
