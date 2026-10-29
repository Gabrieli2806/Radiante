package com.g2806.radiante.client.command;

import com.g2806.radiante.client.gui.BackgroundRebuild;
import com.g2806.radiante.client.gui.ShaderPackHotReload;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.ShaderPackLibrary;
import com.g2806.radiante.client.pipeline.ShaderPackValidation;
import com.mojang.brigadier.Command;
import com.mojang.brigadier.arguments.StringArgumentType;
import com.mojang.brigadier.builder.LiteralArgumentBuilder;
import com.mojang.brigadier.builder.RequiredArgumentBuilder;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.suggestion.SuggestionProvider;
import com.mojang.brigadier.suggestion.Suggestions;
import com.mojang.brigadier.suggestion.SuggestionsBuilder;
import net.minecraft.ChatFormatting;
import net.minecraft.client.Minecraft;
import net.minecraft.network.chat.Component;

import java.util.List;
import java.util.Locale;
import java.util.concurrent.CompletableFuture;

/**
 * Radiante's client commands, the same on every loader: each loader registers {@link #build()} with its own client
 * command source ({@code RadianteFabric}, {@code RadianteNeoForge}, {@code RadianteForge}). Replies go to the chat
 * directly, so nothing here depends on the source type.
 *
 * <ul>
 *   <li>{@code /radiante pack validate [name]}: loads and compiles a pack from {@code radiante/shaderpacks/} (the
 *       active one without a name) without switching to it, and prints the report.</li>
 *   <li>{@code /radiante pack reload}: checks the active folder pack and builds it again when it compiles, as an
 *       edit to its files does by itself ({@link ShaderPackHotReload}).</li>
 *   <li>{@code /radiante debug view [view|off]}: shows a guide image (albedo, normals...) or one of the pack's debug
 *       views as the picture; without a view, lists them.</li>
 * </ul>
 */
public final class RadianteCommands {

    /** Lines of a failed check printed in the chat; the rest is on the Shader Packs screen and in the log. */
    private static final int CHAT_REPORT_LINES = 12;

    private RadianteCommands() {
    }

    public static <S> LiteralArgumentBuilder<S> build() {
        SuggestionProvider<S> packs = (context, builder) -> suggestPacks(builder);
        SuggestionProvider<S> views = (context, builder) -> suggestViews(builder);
        return LiteralArgumentBuilder.<S>literal("radiante")
            // Every subcommand reaches the native renderer; without it the command is not offered.
            .requires(source -> com.g2806.radiante.client.RadianteClient.isNativeLoaded())
            .then(LiteralArgumentBuilder.<S>literal("pack")
                .then(LiteralArgumentBuilder.<S>literal("validate")
                    .executes(context -> validate(null))
                    .then(RequiredArgumentBuilder.<S, String>argument("name", StringArgumentType.greedyString())
                        .suggests(packs)
                        .executes(context -> validate(StringArgumentType.getString(context, "name")))))
                .then(LiteralArgumentBuilder.<S>literal("reload")
                    .executes(context -> reload())))
            .then(LiteralArgumentBuilder.<S>literal("debug")
                .then(LiteralArgumentBuilder.<S>literal("view")
                    .executes(RadianteCommands::listViews)
                    .then(RequiredArgumentBuilder.<S, String>argument("view", StringArgumentType.word())
                        .suggests(views)
                        .executes(context -> setView(StringArgumentType.getString(context, "view"))))));
    }

    private static int validate(String name) {
        String path;
        if (name == null || name.isBlank()) {
            path = Pipeline.shaderPackPath();
        } else {
            path = findPack(name.trim());
            if (path == null) {
                say(Component.translatable("command.radiante.pack.unknown", name.trim()).withStyle(ChatFormatting.RED));
                return 0;
            }
        }
        if (path == null) {
            say(Component.translatable("command.radiante.pack.no_pipeline").withStyle(ChatFormatting.RED));
            return 0;
        }
        String shown = displayName(path);
        boolean started = ShaderPackHotReload.startCheck(Minecraft.getInstance(), path, false,
            result -> report(shown, result));
        if (!started) {
            say(Component.translatable("command.radiante.pack.busy").withStyle(ChatFormatting.YELLOW));
            return 0;
        }
        say(Component.translatable("command.radiante.pack.validate.start", shown).withStyle(ChatFormatting.GRAY));
        return Command.SINGLE_SUCCESS;
    }

    private static void report(String shown, ShaderPackValidation result) {
        if (result.ok()) {
            say(Component.translatable("command.radiante.pack.validate.ok", shown, result.shaders(), result.compiled())
                .withStyle(ChatFormatting.GREEN));
            return;
        }
        List<String> lines = result.reportLines();
        say(Component.translatable("command.radiante.pack.validate.failed", shown, result.errors().size())
            .withStyle(ChatFormatting.RED));
        for (int i = 0; i < Math.min(lines.size(), CHAT_REPORT_LINES); i++) {
            say(Component.literal(lines.get(i)).withStyle(ChatFormatting.GRAY));
        }
        if (lines.size() > CHAT_REPORT_LINES) {
            say(Component.translatable("command.radiante.pack.validate.more", lines.size() - CHAT_REPORT_LINES)
                .withStyle(ChatFormatting.YELLOW));
        }
    }

    private static int reload() {
        if (!ShaderPackHotReload.canReload()) {
            say(Component.translatable("command.radiante.pack.reload.not_folder").withStyle(ChatFormatting.YELLOW));
            return 0;
        }
        String path = Pipeline.shaderPackPath();
        String shown = displayName(path);
        boolean started = ShaderPackHotReload.startCheck(Minecraft.getInstance(), path, true, result -> {
            if (result.ok()) {
                say(Component.translatable("command.radiante.pack.reload.ok", shown).withStyle(ChatFormatting.GREEN));
            } else {
                report(shown, result);
            }
        });
        if (!started) {
            say(Component.translatable("command.radiante.pack.busy").withStyle(ChatFormatting.YELLOW));
            return 0;
        }
        return Command.SINGLE_SUCCESS;
    }

    private static <S> int listViews(CommandContext<S> context) {
        say(Component.translatable("command.radiante.debug.view.list", Pipeline.debugView(),
            String.join(", ", Pipeline.availableDebugViews())));
        return Command.SINGLE_SUCCESS;
    }

    private static int setView(String view) {
        String value = view.toLowerCase(Locale.ROOT);
        if (!value.equals(Pipeline.DEBUG_VIEW_OFF) && !Pipeline.availableDebugViews().contains(value)
            && !Pipeline.availableDebugViews().contains(view)) {
            say(Component.translatable("command.radiante.debug.view.unknown", view,
                String.join(", ", Pipeline.availableDebugViews())).withStyle(ChatFormatting.RED));
            return 0;
        }
        // A pack's own view keeps its spelling; Radiante PT's are lower case.
        String chosen = Pipeline.availableDebugViews().contains(view) ? view : value;
        if (Pipeline.setDebugView(chosen)) {
            BackgroundRebuild.apply(Minecraft.getInstance());
        }
        say(chosen.equals(Pipeline.DEBUG_VIEW_OFF) ? Component.translatable("command.radiante.debug.view.off")
            : Component.translatable("command.radiante.debug.view.set", chosen));
        return Command.SINGLE_SUCCESS;
    }

    /** A pack in radiante/shaderpacks/ by its file or folder name (".zip" optional) or display name. */
    private static String findPack(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        for (ShaderPackLibrary.Entry entry : ShaderPackLibrary.scan()) {
            String file = entry.fileName().toLowerCase(Locale.ROOT);
            if (file.equals(lower) || file.equals(lower + ".zip")
                || entry.displayName() != null && entry.displayName().toLowerCase(Locale.ROOT).equals(lower)) {
                return entry.path();
            }
        }
        return null;
    }

    private static String displayName(String path) {
        int slash = path.replace('\\', '/').lastIndexOf('/');
        return path.substring(slash + 1);
    }

    private static CompletableFuture<Suggestions> suggestPacks(SuggestionsBuilder builder) {
        String typed = builder.getRemaining().toLowerCase(Locale.ROOT);
        for (ShaderPackLibrary.Entry entry : ShaderPackLibrary.scan()) {
            if (!entry.builtIn() && entry.fileName().toLowerCase(Locale.ROOT).startsWith(typed)) {
                builder.suggest(entry.fileName());
            }
        }
        return builder.buildFuture();
    }

    private static CompletableFuture<Suggestions> suggestViews(SuggestionsBuilder builder) {
        String typed = builder.getRemaining().toLowerCase(Locale.ROOT);
        if (Pipeline.DEBUG_VIEW_OFF.startsWith(typed)) {
            builder.suggest(Pipeline.DEBUG_VIEW_OFF);
        }
        for (String view : Pipeline.availableDebugViews()) {
            if (view.toLowerCase(Locale.ROOT).startsWith(typed)) {
                builder.suggest(view);
            }
        }
        return builder.buildFuture();
    }

    private static void say(Component message) {
        Minecraft minecraft = Minecraft.getInstance();
        if (minecraft.player != null) {
            minecraft.player.sendSystemMessage(message);
        }
    }
}
