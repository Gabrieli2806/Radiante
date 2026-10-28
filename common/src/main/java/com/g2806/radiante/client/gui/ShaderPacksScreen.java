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
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.client.renderer.RenderPipelines;
import net.minecraft.client.renderer.texture.DynamicTexture;
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
 * configs.json was rejected) is shown with the pack, with a button that copies it.
 *
 * <p>Laid out like the settings and the Presets screen ({@link ShareSettingsScreen}), so the three read as one place:
 * the mod and the folder on the left, the packs as flat rows in the middle - picked with a click, switched to with a
 * second one, Use or Enter - what the picked pack is on the right, and flat buttons along the bottom.
 */
public final class ShaderPacksScreen extends Screen {

    // The settings screen's own (SettingsLayout), so the screens read as one place.
    private static final int ACCENT = 0xFF8FE3C8;
    private static final int TEXT = 0xFFFFFFFF;
    private static final int TEXT_DIM = 0xFFB0B8B4;
    private static final int PANEL = 0xC0101212;
    private static final int PANEL_DARK = 0xD0060707;
    private static final int ROW_HOVER = 0x30FFFFFF;
    private static final int DISABLED = 0xFF606060;
    private static final int BAD = 0xFFFF8080;
    private static final int ROW = 20;
    private static final int HEADER = 22;
    private static final int GAP = 6;
    private static final int PAD = 8;
    private static final int ENTRY = 18;
    private static final int BUTTON = 16;
    /** The side of a pack's icon in its row, and in the panel. */
    private static final int ROW_ICON = 16;
    /** An Iris or OptiFine pack put in Radiante's folder: named as one in its row rather than marked as broken. */
    private static final String IRIS_PROBLEM = "screen.radiante.shader_packs.problem.iris";
    private static final int PANEL_ICON = 32;
    /** Icons by pack path and content, kept for the session as the addons' are. */
    private static final Map<String, Optional<Identifier>> ICONS = new HashMap<>();
    private static final Identifier RADIANTE_ICON =
        Identifier.fromNamespaceAndPath("radiante", "textures/gui/settings/icon.png");

    /** Something to click: a sidebar entry or a bottom button. */
    private record Action(Component label, Component tooltip, Runnable run, boolean enabled, int x, int y, int w,
                          int h) {
    }

    /** The screen to go back to, made when leaving: a settings screen has to read the new pack's settings. */
    private final Supplier<Screen> back;
    private final List<Action> entries = new ArrayList<>();
    private final List<Action> buttons = new ArrayList<>();
    private List<Entry> packs = List.of();
    /** The packs the search lets through, in list order. */
    private List<Entry> shown = List.of();
    /** The picked pack, by path; kept while the screen is laid out again (a resize, a search). */
    private String selected;
    private boolean pickedFirst;
    private double scroll;
    private Component notice;
    private String typedSearch = "";
    private EditBox search;

    private int top;
    private int bottom;
    private int sidebarX;
    private int sidebarW;
    private int listX;
    private int listW;
    private int panelX;
    private int panelW;

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
        this.packs = ShaderPackLibrary.scan();
        // Opens on the pack that just failed, whose report is what the player came back to, or else on the one in
        // use, so the list starts from where the player is.
        if (!this.pickedFirst) {
            this.pickedFirst = true;
            ShaderPackTrial.Failure failure = ShaderPackTrial.lastFailure();
            this.selected = failure != null ? failure.packPath() : Pipeline.shaderPackPath();
        }
        filter();

        int margin = Math.max(8, this.width / 24);
        this.top = 40;
        this.bottom = this.height - 30;
        this.sidebarX = margin;
        this.sidebarW = Math.max(128, Math.min(150, this.width / 4));
        this.listX = this.sidebarX + this.sidebarW + GAP;
        int remaining = this.width - margin - this.listX;
        this.panelW = remaining > 290 ? Math.min(200, remaining / 3) : 0;
        this.listW = remaining - (this.panelW > 0 ? this.panelW + GAP : 0);
        this.panelX = this.listX + this.listW + GAP;
        this.scroll = clampScroll(this.scroll);

        this.search = new EditBox(this.font, this.sidebarX, 12, this.width - margin * 2, 18,
            Component.translatable("options.radiante.search"));
        this.search.setHint(Component.translatable("options.radiante.search").withStyle(ChatFormatting.DARK_GRAY));
        this.search.setValue(this.typedSearch);
        this.search.setResponder(text -> {
            this.typedSearch = text;
            this.scroll = 0.0;
            filter();
        });
        addRenderableWidget(this.search);

        // ---- the sidebar: the folder the packs come from ----
        this.entries.clear();
        int y = this.top + 38;
        this.entries.add(entry("screen.radiante.shader_packs.open_folder",
            "screen.radiante.shader_packs.open_folder.tooltip", y, () -> {
                Path folder = ShaderPackLibrary.folder();
                if (folder != null) {
                    Blaze3D.openPath(folder);
                }
            }));
        y += ENTRY;
        this.entries.add(entry("screen.radiante.shader_packs.refresh", null, y,
            () -> this.minecraft.gui.setScreen(reopened())));

        // ---- the bottom: what is done with the picked one ----
        this.buttons.clear();
        Entry picked = picked();
        int buttonY = this.height - 24;
        int x = this.width - margin;
        x = button("gui.done", null, this::onClose, true, x, buttonY, true);
        button("screen.radiante.shader_packs.use", "screen.radiante.shader_packs.use.tooltip",
            () -> choose(picked()), picked != null && !active(picked) && picked.problem() == null, x, buttonY, true);
        ShaderPackTrial.Failure failure = ShaderPackTrial.lastFailure();
        if (failure != null) {
            button("screen.radiante.shader_packs.copy_error", null,
                () -> this.minecraft.keyboardHandler.setClipboard(fullReport(failure)), true, this.sidebarX, buttonY,
                false);
        }
    }

    private Action entry(String key, String tooltipKey, int y, Runnable run) {
        return new Action(Component.translatable(key), tooltipKey == null ? null : Component.translatable(tooltipKey),
            run, true, this.sidebarX, y, this.sidebarW, ENTRY);
    }

    /** Adds a bottom button growing leftwards from {@code x}, or rightwards; returns where the next one goes. */
    private int button(String key, String tooltipKey, Runnable run, boolean enabled, int x, int y, boolean leftwards) {
        Component label = Component.translatable(key);
        int w = this.font.width(label) + 16;
        int left = leftwards ? x - w : x;
        this.buttons.add(new Action(label, tooltipKey == null ? null : Component.translatable(tooltipKey), run, enabled,
            left, y, w, BUTTON));
        return leftwards ? left - 4 : left + w + 4;
    }

    private void filter() {
        String text = this.typedSearch.trim().toLowerCase(Locale.ROOT);
        List<Entry> matches = new ArrayList<>();
        for (Entry entry : this.packs) {
            if (text.isEmpty() || entry.displayName().toLowerCase(Locale.ROOT).contains(text)) {
                matches.add(entry);
            }
        }
        this.shown = matches;
    }

    private Entry picked() {
        if (this.selected == null) {
            return null;
        }
        for (Entry entry : this.packs) {
            if (ShaderPackLibrary.samePack(entry.path(), this.selected)) {
                return entry;
            }
        }
        return null;
    }

    private int pickedRow() {
        for (int i = 0; i < this.shown.size(); i++) {
            if (this.selected != null && ShaderPackLibrary.samePack(this.shown.get(i).path(), this.selected)) {
                return i;
            }
        }
        return -1;
    }

    private static boolean active(Entry entry) {
        return ShaderPackLibrary.samePack(entry.path(), Pipeline.shaderPackPath());
    }

    private static boolean failedLastTime(Entry entry) {
        ShaderPackTrial.Failure failure = ShaderPackTrial.lastFailure();
        return failure != null && ShaderPackLibrary.samePack(failure.packPath(), entry.path());
    }

    private static String fullReport(ShaderPackTrial.Failure failure) {
        return failure.packName() + " (" + failure.packPath() + ")\n" + String.join("\n", failure.lines());
    }

    // ---- what the clicks do ----

    private void pick(int row) {
        this.selected = this.shown.get(row).path();
        Entry entry = this.shown.get(row);
        this.notice = entry.problem() == null ? null : Component.translatable(entry.problem(), entry.problemDetail());
        showPicked();
        this.rebuildWidgets();
    }

    /** Switches to a pack, or says why it cannot be. */
    private void choose(Entry entry) {
        if (entry == null) {
            return;
        }
        if (entry.problem() != null) {
            this.notice = Component.translatable(entry.problem(), entry.problemDetail());
            return;
        }
        if (active(entry)) {
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

    /** Scrolls the list so that the picked pack is in view. */
    private void showPicked() {
        int row = pickedRow();
        if (row < 0) {
            return;
        }
        int view = this.bottom - this.top - HEADER;
        int rowTop = row * ROW;
        if (rowTop < this.scroll) {
            this.scroll = rowTop;
        } else if (rowTop + ROW > this.scroll + view) {
            this.scroll = rowTop + ROW - view;
        }
        this.scroll = clampScroll(this.scroll);
    }

    private double clampScroll(double value) {
        int view = this.bottom - this.top - HEADER;
        return Math.max(0.0, Math.min(value, Math.max(0, this.shown.size() * ROW - view)));
    }

    // ---- input ----

    @Override
    public boolean mouseClicked(MouseButtonEvent event, boolean doubleClick) {
        if (super.mouseClicked(event, doubleClick)) {
            return true;
        }
        double x = event.x();
        double y = event.y();
        for (List<Action> group : List.of(this.entries, this.buttons)) {
            for (Action action : group) {
                if (action.enabled() && inside(x, y, action.x(), action.y(), action.w(), action.h())) {
                    AbstractWidget.playButtonClickSound(this.minecraft.getSoundManager());
                    action.run().run();
                    return true;
                }
            }
        }
        int listTop = this.top + HEADER;
        if (inside(x, y, this.listX, listTop, this.listW, this.bottom - listTop)) {
            int row = (int) ((y - listTop + this.scroll) / ROW);
            if (row >= 0 && row < this.shown.size()) {
                AbstractWidget.playButtonClickSound(this.minecraft.getSoundManager());
                this.setFocused(null);
                // A switch rebuilds the pipeline, so a click only picks the pack: a second click or Use switches.
                if (doubleClick && row == pickedRow()) {
                    choose(this.shown.get(row));
                    return true;
                }
                pick(row);
                return true;
            }
        }
        return false;
    }

    /**
     * Up and Down move through the list (from the search field too), Enter or Space switches to the picked pack;
     * read through the event's own tests, as the settings screen does, so controller mods that press keys reach it.
     */
    @Override
    public boolean keyPressed(KeyEvent event) {
        if ((event.isUp() || event.isDown()) && !this.shown.isEmpty()) {
            int row = pickedRow();
            int next = row < 0 ? 0 : Math.max(0, Math.min(this.shown.size() - 1, row + (event.isUp() ? -1 : 1)));
            if (event.isUp() && row == 0 && this.search != null) {
                this.setFocused(this.search);
                return true;
            }
            this.setFocused(null);
            pick(next);
            return true;
        }
        // Space is typed into the search field, so a selection key only switches with the field not in use.
        if (event.isSelection() && this.getFocused() != this.search) {
            choose(picked());
            return true;
        }
        return super.keyPressed(event);
    }

    @Override
    protected void setInitialFocus() {
        // The list, not the search field: Enter switches to the pack in view straight away.
    }

    @Override
    public boolean mouseScrolled(double mouseX, double mouseY, double scrollX, double scrollY) {
        if (inside(mouseX, mouseY, this.listX, this.top, this.listW, this.bottom - this.top)) {
            this.scroll = clampScroll(this.scroll - scrollY * ROW);
            return true;
        }
        return super.mouseScrolled(mouseX, mouseY, scrollX, scrollY);
    }

    private static boolean inside(double mouseX, double mouseY, int x, int y, int w, int h) {
        return mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + h;
    }

    // ---- drawing ----

    @Override
    public void extractRenderState(GuiGraphicsExtractor g, int mouseX, int mouseY, float partialTick) {
        super.extractRenderState(g, mouseX, mouseY, partialTick);
        drawSidebar(g, mouseX, mouseY);
        drawList(g, mouseX, mouseY);
        drawDetails(g);
        for (Action button : this.buttons) {
            boolean hover = button.enabled() && inside(mouseX, mouseY, button.x(), button.y(), button.w(), button.h());
            g.fill(button.x(), button.y(), button.x() + button.w(), button.y() + BUTTON, hover ? PANEL_DARK : PANEL);
            if (hover) {
                g.fill(button.x(), button.y() + BUTTON - 1, button.x() + button.w(), button.y() + BUTTON, ACCENT);
            }
            g.centeredText(this.font, button.label(), button.x() + button.w() / 2, button.y() + 4,
                !button.enabled() ? DISABLED : hover ? ACCENT : TEXT);
            tooltip(g, button, mouseX, mouseY);
        }
    }

    private void drawSidebar(GuiGraphicsExtractor g, int mouseX, int mouseY) {
        g.fill(this.sidebarX, this.top, this.sidebarX + this.sidebarW, this.bottom, PANEL);
        int headerY = this.top + 6;
        g.blit(RenderPipelines.GUI_TEXTURED, RADIANTE_ICON, this.sidebarX + PAD, headerY, 0.0f, 0.0f, 24, 24, 24, 24);
        int nameX = this.sidebarX + PAD + 30;
        g.text(this.font, Component.literal("Radiante").withStyle(ChatFormatting.BOLD), nameX, headerY + 3, TEXT);
        g.text(this.font, this.title, nameX, headerY + 14, TEXT_DIM);
        int end = this.top + 38;
        for (Action entry : this.entries) {
            boolean hover = inside(mouseX, mouseY, entry.x(), entry.y(), entry.w(), entry.h());
            if (hover) {
                g.fill(entry.x(), entry.y(), entry.x() + entry.w(), entry.y() + ENTRY, ROW_HOVER);
                g.fill(entry.x() + entry.w() - 3, entry.y(), entry.x() + entry.w(), entry.y() + ENTRY, ACCENT);
            }
            g.text(this.font, Component.literal("› ").withColor(ACCENT), entry.x() + PAD, entry.y() + 5, ACCENT);
            g.text(this.font, Component.literal(this.font.plainSubstrByWidth(entry.label().getString(),
                entry.w() - PAD * 2 - 10)), entry.x() + PAD + 10, entry.y() + 5, hover ? ACCENT : TEXT);
            tooltip(g, entry, mouseX, mouseY);
            end = entry.y() + ENTRY;
        }
        // Where packs go, in the space under the entries.
        int y = end + 10;
        for (FormattedCharSequence line : this.font.split(Component.translatable("screen.radiante.shader_packs.hint",
            ShaderPackLibrary.FOLDER), this.sidebarW - PAD * 2)) {
            if (y + 9 > this.bottom - 4) {
                break;
            }
            g.text(this.font, line, this.sidebarX + PAD, y, DISABLED);
            y += 10;
        }
    }

    private void drawList(GuiGraphicsExtractor g, int mouseX, int mouseY) {
        g.fill(this.listX, this.top, this.listX + this.listW, this.bottom, PANEL);
        g.fill(this.listX, this.top, this.listX + this.listW, this.top + HEADER, PANEL_DARK);
        g.text(this.font, Component.literal("◆ ").append(this.title), this.listX + PAD, this.top + 7, ACCENT);
        Component count = Component.literal(Integer.toString(this.shown.size()));
        g.text(this.font, count, this.listX + this.listW - PAD - this.font.width(count), this.top + 7, TEXT_DIM);

        int listTop = this.top + HEADER;
        int pickedRow = pickedRow();
        g.enableScissor(this.listX, listTop, this.listX + this.listW, this.bottom);
        for (int row = 0; row < this.shown.size(); row++) {
            int y = listTop + row * ROW - (int) this.scroll;
            if (y + ROW <= listTop || y >= this.bottom) {
                continue;
            }
            Entry entry = this.shown.get(row);
            boolean hover = inside(mouseX, mouseY, this.listX, Math.max(y, listTop), this.listW, ROW)
                && mouseY >= listTop && mouseY < this.bottom;
            if (row == pickedRow) {
                g.fill(this.listX + 4, y, this.listX + this.listW - 4, y + ROW, PANEL_DARK);
                g.fill(this.listX + 4, y, this.listX + 7, y + ROW, ACCENT);
            } else if (hover) {
                g.fill(this.listX + 4, y, this.listX + this.listW - 4, y + ROW, ROW_HOVER);
            }
            int left = this.listX + PAD + 8;
            Identifier icon = icon(entry);
            if (icon != null) {
                g.blit(icon, left, y + 2, left + ROW_ICON, y + 2 + ROW_ICON, 0.0f, 1.0f, 0.0f, 1.0f);
            } else {
                g.fill(left, y + 2, left + ROW_ICON, y + 2 + ROW_ICON, 0x40FFFFFF);
            }
            left += ROW_ICON + 6;
            // On the right: in use, or why it cannot be or did not work, or that it comes with Radiante.
            int right = this.listX + this.listW - PAD - 4;
            Component tag = null;
            int tagColor = TEXT_DIM;
            if (active(entry)) {
                tag = Component.translatable("screen.radiante.shader_packs.active");
                tagColor = ACCENT;
            } else if (IRIS_PROBLEM.equals(entry.problem())) {
                tag = Component.literal("Iris / OptiFine");
                tagColor = TEXT_DIM;
            } else if (entry.problem() != null || failedLastTime(entry)) {
                tag = Component.literal("!");
                tagColor = BAD;
            } else if (ShaderPackTrial.trying(entry.path())) {
                tag = Component.literal("...");
                tagColor = ACCENT;
            } else if (entry.builtIn()) {
                tag = Component.translatable("screen.radiante.shader_packs.built_in");
            }
            if (tag != null) {
                g.text(this.font, tag, right - this.font.width(tag), y + 6, tagColor);
                right -= this.font.width(tag) + 8;
            }
            String name = this.font.plainSubstrByWidth(entry.displayName(), Math.max(0, right - left));
            int color = entry.problem() != null ? TEXT_DIM : row == pickedRow ? ACCENT : TEXT;
            g.text(this.font, Component.literal(name), left, y + 6, color);
        }
        g.disableScissor();
        int view = this.bottom - listTop;
        int content = this.shown.size() * ROW;
        if (content > view) {
            int bar = Math.max(16, view * view / content);
            int barY = listTop + (int) ((view - bar) * this.scroll / (content - view));
            g.fill(this.listX + this.listW - 2, barY, this.listX + this.listW, barY + bar, ACCENT);
        }
    }

    /**
     * What the picked pack is, why it cannot be used or did not work (with the renderer's messages), and what just
     * happened: in the panel beside the list, or along the foot of the list when narrow.
     */
    private void drawDetails(GuiGraphicsExtractor g) {
        List<FormattedCharSequence> lines = new ArrayList<>();
        List<Integer> colors = new ArrayList<>();
        int wrap = this.panelW > 0 ? this.panelW - PAD * 2 : this.listW - PAD * 2;
        Entry picked = picked();
        int textTop = this.top + PAD;
        if (this.panelW > 0 && picked != null) {
            // The name beside the icon, the rest under both.
            int nameWrap = wrap - PANEL_ICON - 6;
            List<FormattedCharSequence> name = this.font.split(
                Component.literal(picked.displayName()).withStyle(ChatFormatting.BOLD), nameWrap);
            List<FormattedCharSequence> details = this.font.split(details(picked), nameWrap);
            g.fill(this.panelX, this.top, this.panelX + this.panelW, this.bottom, PANEL);
            int x = this.panelX + PAD;
            Identifier icon = icon(picked);
            if (icon != null) {
                g.blit(icon, x, textTop, x + PANEL_ICON, textTop + PANEL_ICON, 0.0f, 1.0f, 0.0f, 1.0f);
            } else {
                g.fill(x, textTop, x + PANEL_ICON, textTop + PANEL_ICON, 0x40FFFFFF);
            }
            int y = textTop + 2;
            for (FormattedCharSequence line : name) {
                g.text(this.font, line, x + PANEL_ICON + 6, y, ACCENT);
                y += 10;
            }
            for (FormattedCharSequence line : details) {
                g.text(this.font, line, x + PANEL_ICON + 6, y, TEXT_DIM);
                y += 10;
            }
            textTop = Math.max(textTop + PANEL_ICON, y) + 8;
            if (picked.description() != null && !picked.description().isBlank()) {
                add(lines, colors, Component.literal(picked.description()), TEXT, wrap);
                blank(lines, colors);
            }
        }
        if (picked != null) {
            if (picked.problem() != null) {
                add(lines, colors, Component.translatable(picked.problem(), picked.problemDetail()), BAD, wrap);
            } else if (ShaderPackTrial.trying(picked.path())) {
                add(lines, colors, Component.translatable("screen.radiante.shader_packs.trying"), ACCENT, wrap);
            } else if (this.panelW > 0) {
                add(lines, colors, Component.translatable(active(picked) ? "screen.radiante.shader_packs.hint.in_use"
                    : "screen.radiante.shader_packs.hint.use"), TEXT_DIM, wrap);
            }
            ShaderPackTrial.Failure failure = ShaderPackTrial.lastFailure();
            if (failure != null && failedLastTime(picked)) {
                blank(lines, colors);
                add(lines, colors, Component.translatable("screen.radiante.shader_packs.failed", failure.packName()),
                    BAD, wrap);
                for (String line : failure.lines().isEmpty()
                    ? List.of(Component.translatable("screen.radiante.shader_packs.failed.no_detail").getString())
                    : failure.lines()) {
                    add(lines, colors, Component.literal(line), TEXT_DIM, wrap);
                }
            }
        } else if (this.panelW > 0) {
            add(lines, colors, Component.translatable("screen.radiante.shader_packs.hint.pick"), TEXT_DIM, wrap);
        }
        if (this.notice != null && (picked == null || picked.problem() == null)) {
            if (!lines.isEmpty()) {
                blank(lines, colors);
            }
            add(lines, colors, this.notice, BAD, wrap);
        }
        if (this.panelW > 0) {
            if (picked == null) {
                g.fill(this.panelX, this.top, this.panelX + this.panelW, this.bottom, PANEL);
            }
            int y = textTop;
            for (int i = 0; i < lines.size() && y + 9 <= this.bottom - 4; i++) {
                g.text(this.font, lines.get(i), this.panelX + PAD, y, colors.get(i));
                y += 10;
            }
        } else if (!lines.isEmpty()) {
            // No room for the panel: as much as fits along the foot of the list, up to a third of it.
            int most = Math.max(2, (this.bottom - this.top) / 3 / 10);
            List<FormattedCharSequence> foot = lines.subList(0, Math.min(lines.size(), most));
            int y = this.bottom - 4 - foot.size() * 10;
            g.fill(this.listX, y - 4, this.listX + this.listW, this.bottom, PANEL_DARK);
            for (int i = 0; i < foot.size(); i++) {
                g.text(this.font, foot.get(i), this.listX + PAD, y, colors.get(i));
                y += 10;
            }
        }
    }

    /** Version, author and license, those the pack gives, or its file name. */
    private static Component details(Entry entry) {
        List<String> parts = new ArrayList<>();
        if (entry.version() != null) {
            parts.add(entry.version());
        }
        if (entry.author() != null) {
            parts.add(entry.author());
        }
        if (entry.license() != null) {
            parts.add(entry.license());
        }
        if (parts.isEmpty()) {
            parts.add(entry.fileName());
        }
        Component text = Component.literal(String.join("  ·  ", parts));
        return entry.builtIn() ? Component.empty().append(text).append("  ·  ")
            .append(Component.translatable("screen.radiante.shader_packs.built_in")) : text;
    }

    private void add(List<FormattedCharSequence> lines, List<Integer> colors, Component text, int color, int wrap) {
        for (FormattedCharSequence line : this.font.split(text, wrap)) {
            lines.add(line);
            colors.add(color);
        }
    }

    private static void blank(List<FormattedCharSequence> lines, List<Integer> colors) {
        lines.add(FormattedCharSequence.EMPTY);
        colors.add(TEXT);
    }

    private void tooltip(GuiGraphicsExtractor g, Action action, int mouseX, int mouseY) {
        if (action.tooltip() != null && inside(mouseX, mouseY, action.x(), action.y(), action.w(), action.h())) {
            g.setTooltipForNextFrame(this.font.split(action.tooltip(), 200), mouseX, mouseY);
        }
    }

    @Override
    public void onClose() {
        this.minecraft.gui.setScreen(this.back.get());
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
}
