package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.ShaderPackLibrary;
import com.g2806.radiante.client.pipeline.ShaderPackLibrary.Entry;
import com.mojang.blaze3d.Blaze3D;
import com.mojang.blaze3d.platform.NativeImage;
import net.minecraft.ChatFormatting;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.ObjectSelectionList;
import net.minecraft.client.gui.components.Tooltip;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.client.renderer.texture.DynamicTexture;
import net.minecraft.network.chat.CommonComponents;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.util.FormattedCharSequence;

import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.function.Supplier;

/**
 * Lists the built-in Radiante PT and the shader packs in {@code radiante/shaderpacks/} (see
 * {@link ShaderPackLibrary}), and switches the pipeline to the one chosen: the pipeline is rebuilt as for any other
 * setting that changes shaders ({@link BackgroundRebuild}), and {@link ShaderPackTrial} watches the new pack until it
 * runs, going back to the built-in one when it fails. What the renderer said then (the compiler's messages, or why
 * configs.json was rejected) is shown under the list, with a button that copies it.
 *
 * <p>A vanilla selection list and buttons, so the keys, Tab and a controller move through it as through the game's
 * own lists: a click picks a pack, and Enter, a double click or Use switches to it.
 */
public final class ShaderPacksScreen extends Screen {

    private static final int ACCENT = 0xFF8FE3C8;
    private static final int TEXT_DIM = 0xFFB0B8B4;
    private static final int ERROR = 0xFFFF7070;
    private static final int ROW = 36;
    /** Icons by pack path and content, kept for the session as the addons' are. */
    private static final Map<String, Optional<Identifier>> ICONS = new HashMap<>();
    private static final Identifier RADIANTE_ICON =
        Identifier.fromNamespaceAndPath("radiante", "textures/gui/settings/icon.png");

    /** The screen to go back to, made when leaving: a settings screen has to read the new pack's settings. */
    private final Supplier<Screen> back;
    private PackList list;
    private Component notice;
    private List<FormattedCharSequence> report = List.of();

    public ShaderPacksScreen(Supplier<Screen> back) {
        super(Component.translatable("screen.radiante.shader_packs.title"));
        this.back = back;
    }

    /** This screen made again, to show what changed (the pack in use, a failure). */
    Screen reopened() {
        return new ShaderPacksScreen(this.back);
    }

    @Override
    protected void init() {
        ShaderPackTrial.Failure failure = ShaderPackTrial.lastFailure();
        int reportHeight = 0;
        if (failure != null) {
            List<FormattedCharSequence> lines = new ArrayList<>();
            int width = Math.min(this.width - 40, 520);
            for (String line : failure.lines().isEmpty()
                ? List.of(Component.translatable("screen.radiante.shader_packs.failed.no_detail").getString())
                : failure.lines()) {
                lines.addAll(this.font.split(Component.literal(line), width));
            }
            this.report = lines;
            reportHeight = Math.min(lines.size(), Math.max(3, (this.height - 160) / 3 / 10)) * 10 + 16;
        } else {
            this.report = List.of();
        }
        int listTop = 32;
        int listBottom = this.height - 36 - reportHeight;
        this.list = new PackList(this.minecraft, this.width, listBottom - listTop, listTop);
        String active = Pipeline.shaderPackPath();
        PackRow selected = null;
        for (Entry entry : ShaderPackLibrary.scan()) {
            PackRow row = new PackRow(entry);
            this.list.add(row);
            if (active != null && ShaderPackLibrary.samePack(entry.path(), active)) {
                selected = row;
            }
        }
        if (selected != null) {
            this.list.setSelected(selected);
        }
        addRenderableWidget(this.list);

        List<Button> buttons = new ArrayList<>();
        buttons.add(Button.builder(Component.translatable("screen.radiante.shader_packs.use"), button -> {
            PackRow row = this.list.getSelected();
            if (row != null) {
                choose(row.entry);
            }
        }).tooltip(Tooltip.create(Component.translatable("screen.radiante.shader_packs.use.tooltip"))).build());
        buttons.add(Button.builder(Component.translatable("screen.radiante.shader_packs.open_folder"), button -> {
            Path folder = ShaderPackLibrary.folder();
            if (folder != null) {
                Blaze3D.openPath(folder);
            }
        }).tooltip(Tooltip.create(Component.translatable("screen.radiante.shader_packs.open_folder.tooltip"))).build());
        buttons.add(Button.builder(Component.translatable("screen.radiante.shader_packs.refresh"),
            button -> this.minecraft.gui.setScreen(reopened())).build());
        if (failure != null) {
            buttons.add(Button.builder(Component.translatable("screen.radiante.shader_packs.copy_error"),
                button -> this.minecraft.keyboardHandler.setClipboard(fullReport(failure))).build());
        }
        buttons.add(Button.builder(CommonComponents.GUI_DONE, button -> onClose()).build());
        int buttonWidth = Math.min(120, (this.width - 20) / buttons.size() - 4);
        int x = this.width / 2 - (buttons.size() * (buttonWidth + 4) - 4) / 2;
        for (Button button : buttons) {
            button.setRectangle(buttonWidth, 20, x, this.height - 28);
            addRenderableWidget(button);
            x += buttonWidth + 4;
        }
    }

    @Override
    protected void setInitialFocus() {
        setInitialFocus(this.list);
    }

    private static String fullReport(ShaderPackTrial.Failure failure) {
        return failure.packName() + " (" + failure.packPath() + ")\n" + String.join("\n", failure.lines());
    }

    @Override
    public void extractRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
        super.extractRenderState(graphics, mouseX, mouseY, partialTick);
        graphics.centeredText(this.font, this.title, this.width / 2, 8, 0xFFFFFFFF);
        Component hint = this.notice != null ? this.notice
            : Component.translatable("screen.radiante.shader_packs.hint", ShaderPackLibrary.FOLDER);
        graphics.centeredText(this.font, hint, this.width / 2, 19, this.notice != null ? ERROR : TEXT_DIM);
        ShaderPackTrial.Failure failure = ShaderPackTrial.lastFailure();
        if (failure != null && !this.report.isEmpty()) {
            int top = this.list.getBottom() + 4;
            int bottom = this.height - 34;
            int left = this.width / 2 - Math.min(this.width - 40, 520) / 2;
            graphics.fill(left - 4, top, this.width - left + 4, bottom, 0xA0000000);
            graphics.text(this.font, Component.translatable("screen.radiante.shader_packs.failed", failure.packName()),
                left, top + 3, ERROR);
            int y = top + 14;
            for (FormattedCharSequence line : this.report) {
                if (y + 9 > bottom) {
                    break;
                }
                graphics.text(this.font, line, left, y, TEXT_DIM);
                y += 10;
            }
        }
    }

    @Override
    public void onClose() {
        this.minecraft.gui.setScreen(this.back.get());
    }

    /** Switches to a pack, or says why it cannot be. */
    private void choose(Entry entry) {
        if (entry.problem() != null) {
            this.notice = Component.translatable(entry.problem(), entry.problemDetail());
            return;
        }
        if (ShaderPackLibrary.samePack(entry.path(), Pipeline.shaderPackPath())) {
            return;
        }
        if (Pipeline.shaderPackUnavailable(entry.path())) {
            this.notice = Component.translatable("screen.radiante.shader_packs.needs_emission");
            return;
        }
        if (!Pipeline.selectShaderPack(entry.path())) {
            return;
        }
        RadianteClient.LOGGER.info("Shader pack: {}", entry.path());
        ShaderPackTrial.begin(entry.path(), entry.displayName());
        BackgroundRebuild.open(this.minecraft, reopened(), true);
    }

    private static Identifier icon(Entry entry) {
        if (entry.icon() == null) {
            return entry.builtIn() ? RADIANTE_ICON : null;
        }
        String key = entry.path() + "#" + Arrays.hashCode(entry.icon());
        return ICONS.computeIfAbsent(key, ignored -> Optional.ofNullable(loadIcon(entry))).orElse(null);
    }

    private static Identifier loadIcon(Entry entry) {
        NativeImage image = null;
        try {
            image = NativeImage.read(entry.icon());
            NativeImage shown = AddonIcons.shrink(image);
            if (shown != image) {
                image.close();
                image = shown;
            }
            String name = entry.path().toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9/._-]", "_");
            Identifier id = Identifier.fromNamespaceAndPath("radiante",
                "shader_pack_icon/" + name + "_" + Integer.toHexString(Arrays.hashCode(entry.icon())));
            NativeImage pixels = image;
            image = null;
            Minecraft.getInstance().getTextureManager().register(id,
                new DynamicTexture(() -> "Radiante shader pack icon " + entry.path(), pixels));
            return id;
        } catch (IOException | RuntimeException e) {
            RadianteClient.LOGGER.warn("Shader pack {}: its icon is not a PNG that can be read", entry.path(), e);
            return null;
        } finally {
            if (image != null) {
                image.close();
            }
        }
    }

    private final class PackList extends ObjectSelectionList<PackRow> {
        PackList(Minecraft minecraft, int width, int height, int y) {
            super(minecraft, width, height, y, ROW);
        }

        void add(PackRow row) {
            addEntry(row);
        }

        @Override
        public int getRowWidth() {
            return Math.min(this.width - 40, 400);
        }
    }

    private final class PackRow extends ObjectSelectionList.Entry<PackRow> {
        private final Entry entry;

        PackRow(Entry entry) {
            this.entry = entry;
        }

        @Override
        public Component getNarration() {
            return Component.literal(this.entry.displayName());
        }

        @Override
        public void extractContent(GuiGraphicsExtractor graphics, int mouseX, int mouseY, boolean hovered, float a) {
            int x = getContentX();
            int y = getContentY();
            Identifier icon = icon(this.entry);
            if (icon != null) {
                graphics.blit(icon, x, y, x + 32, y + 32, 0.0f, 1.0f, 0.0f, 1.0f);
            } else {
                graphics.fill(x, y, x + 32, y + 32, 0x40FFFFFF);
            }
            int textX = x + 36;
            int width = getContentWidth() - 38;
            boolean active = ShaderPackLibrary.samePack(this.entry.path(), Pipeline.shaderPackPath());
            Component name = Component.literal(this.entry.displayName());
            if (active) {
                name = Component.empty().append(name).append(Component.literal("  "))
                    .append(Component.translatable("screen.radiante.shader_packs.active").withColor(ACCENT));
            } else if (this.entry.builtIn()) {
                name = Component.empty().append(name).append(Component.literal("  "))
                    .append(Component.translatable("screen.radiante.shader_packs.built_in").withStyle(ChatFormatting.GRAY));
            }
            graphics.text(Minecraft.getInstance().font, clip(name, width), textX, y + 1, 0xFFFFFFFF);
            graphics.text(Minecraft.getInstance().font, clip(details(), width), textX, y + 12, TEXT_DIM);
            Component third;
            int color = TEXT_DIM;
            ShaderPackTrial.Failure failure = ShaderPackTrial.lastFailure();
            if (this.entry.problem() != null) {
                third = Component.translatable(this.entry.problem(), this.entry.problemDetail());
                color = ERROR;
            } else if (failure != null && ShaderPackLibrary.samePack(failure.packPath(), this.entry.path())) {
                third = Component.translatable("screen.radiante.shader_packs.failed.short");
                color = ERROR;
            } else if (ShaderPackTrial.trying(this.entry.path())) {
                third = Component.translatable("screen.radiante.shader_packs.trying");
                color = ACCENT;
            } else {
                third = Component.literal(this.entry.description() == null ? "" : this.entry.description());
            }
            graphics.text(Minecraft.getInstance().font, clip(third, width), textX, y + 23, color);
        }

        /** Version, author and license, those the pack gives, or its file name. */
        private Component details() {
            List<String> parts = new ArrayList<>();
            if (this.entry.version() != null) {
                parts.add(this.entry.version());
            }
            if (this.entry.author() != null) {
                parts.add(this.entry.author());
            }
            if (this.entry.license() != null) {
                parts.add(this.entry.license());
            }
            if (parts.isEmpty()) {
                parts.add(this.entry.fileName());
            }
            return Component.literal(String.join("  ·  ", parts));
        }

        private FormattedCharSequence clip(Component text, int width) {
            var font = Minecraft.getInstance().font;
            if (font.width(text) <= width) {
                return text.getVisualOrderText();
            }
            return net.minecraft.locale.Language.getInstance().getVisualOrder(
                font.substrByWidth(text, Math.max(0, width - font.width("..."))));
        }

        @Override
        public boolean mouseClicked(MouseButtonEvent event, boolean doubleClick) {
            ShaderPacksScreen.this.list.setSelected(this);
            // A switch rebuilds the pipeline, so a click only picks the pack: a double click or Use switches.
            ShaderPacksScreen.this.notice = this.entry.problem() == null ? null
                : Component.translatable(this.entry.problem(), this.entry.problemDetail());
            if (doubleClick) {
                choose(this.entry);
            }
            return true;
        }

        @Override
        public boolean keyPressed(KeyEvent event) {
            if (event.isSelection()) {
                choose(this.entry);
                return true;
            }
            return super.keyPressed(event);
        }
    }
}
