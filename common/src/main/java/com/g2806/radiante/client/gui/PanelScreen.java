package com.g2806.radiante.client.gui;

import com.g2806.radiante.compat.VersionCompat;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.ChatFormatting;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.util.FormattedCharSequence;

/**
 * A screen of three columns in the settings screen's look: a sidebar of actions under the mod's name, a list that
 * scrolls, and a panel about the row chosen in it, with buttons along the bottom. Shader Packs and Share Settings
 * are both this; each used to carry its own copy of everything here.
 *
 * <p>A screen gives the list's and the panel's contents and places the columns in its {@code init()}; the sidebar,
 * the buttons, the scrolling and the tooltips are drawn and handled here.
 */
abstract class PanelScreen extends VersionCompat.DrawnScreen {

    protected static final int ACCENT = Theme.ACCENT;
    protected static final int TEXT = Theme.TEXT;
    protected static final int TEXT_DIM = Theme.TEXT_DIM;
    protected static final int PANEL = Theme.PANEL;
    protected static final int PANEL_DARK = Theme.PANEL_DARK;
    protected static final int ROW_HOVER = Theme.ROW_HOVER;
    protected static final int DISABLED = Theme.DISABLED;
    protected static final int BAD = Theme.BAD;
    protected static final int ROW = Theme.ROW;
    protected static final int HEADER = Theme.HEADER;
    protected static final int GAP = Theme.GAP;
    protected static final int PAD = Theme.PAD;
    protected static final int ENTRY = Theme.ENTRY;
    protected static final int BUTTON = Theme.BUTTON;
    protected static final Identifier ICON =
        Identifier.fromNamespaceAndPath("radiante", "textures/gui/settings/icon.png");

    /** Something to click: an entry of the sidebar or a button, with where it is and the colour of its label. */
    protected record Action(Component label, Component tooltip, Runnable run, boolean enabled, int x, int y, int w,
                            int h, int color) {
    }

    protected final List<Action> entries = new ArrayList<>();
    protected final List<Action> buttons = new ArrayList<>();
    protected double scroll;

    // Where the columns are; a screen sets them in init().
    protected int top;
    protected int bottom;
    protected int sidebarX;
    protected int sidebarW;
    protected int listX;
    protected int listW;
    protected int panelX;
    protected int panelW;

    protected PanelScreen(Component title) {
        super(title);
    }

    /** How many rows the list has, for how far it scrolls. */
    protected abstract int rows();

    /** The mark in front of each sidebar entry. */
    protected abstract String bullet();

    /** What the sidebar says under its entries, in the space left. */
    protected abstract Component sidebarHint();

    protected abstract void drawList(GuiGraphicsExtractor g, int mouseX, int mouseY);

    protected abstract void drawDetails(GuiGraphicsExtractor g);

    protected Action entry(String key, String tooltipKey, int y, Runnable run) {
        return new Action(Component.translatable(key), tooltipKey == null ? null : Component.translatable(tooltipKey),
            run, true, this.sidebarX, y, this.sidebarW, ENTRY, TEXT);
    }

    /** Adds a button and returns where the next one goes, to its left or to its right. */
    protected int button(String key, String tooltipKey, Runnable run, boolean enabled, int x, int y,
        boolean leftwards) {
        return button(key, tooltipKey, run, enabled, x, y, leftwards, TEXT);
    }

    protected int button(String key, String tooltipKey, Runnable run, boolean enabled, int x, int y, boolean leftwards,
        int color) {
        Component label = Component.translatable(key);
        int w = this.font.width(label) + 16;
        int left = leftwards ? x - w : x;
        this.buttons.add(new Action(label, tooltipKey == null ? null : Component.translatable(tooltipKey), run, enabled,
            left, y, w, BUTTON, color));
        return leftwards ? left - 4 : left + w + 4;
    }

    protected double clampScroll(double value) {
        int view = this.bottom - this.top - HEADER;
        return Math.max(0.0, Math.min(value, Math.max(0, rows() * ROW - view)));
    }

    @Override
    public boolean mouseScrolled(double mouseX, double mouseY, double scrollX, double scrollY) {
        if (inside(mouseX, mouseY, this.listX, this.top, this.listW, this.bottom - this.top)) {
            this.scroll = clampScroll(this.scroll - scrollY * ROW);
            return true;
        }
        return super.mouseScrolled(mouseX, mouseY, scrollX, scrollY);
    }

    protected static boolean inside(double mouseX, double mouseY, int x, int y, int w, int h) {
        return mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + h;
    }

    @Override
    protected void extractContents(GuiGraphicsExtractor g, int mouseX, int mouseY) {
        drawSidebar(g, mouseX, mouseY);
        drawList(g, mouseX, mouseY);
        drawDetails(g);
        for (Action button : this.buttons) {
            boolean hover = button.enabled() && inside(mouseX, mouseY, button.x(), button.y(), button.w(), button.h());
            g.fill(button.x(), button.y(), button.x() + button.w(), button.y() + BUTTON, hover ? PANEL_DARK : PANEL);
            if (hover) {
                g.fill(button.x(), button.y() + BUTTON - 1, button.x() + button.w(), button.y() + BUTTON, ACCENT);
            }
            // A button in a colour of its own (a warning) keeps it under the pointer.
            g.centeredText(this.font, button.label(), button.x() + button.w() / 2, button.y() + 4,
                !button.enabled() ? DISABLED : hover && button.color() == TEXT ? ACCENT : button.color());
            tooltip(g, button, mouseX, mouseY);
        }
    }

    private void drawSidebar(GuiGraphicsExtractor g, int mouseX, int mouseY) {
        g.fill(this.sidebarX, this.top, this.sidebarX + this.sidebarW, this.bottom, PANEL);
        int headerY = this.top + 6;
        VersionCompat.blit(g, ICON, this.sidebarX + PAD, headerY, 0.0f, 0.0f, 24, 24, 24, 24);
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
            g.text(this.font, Component.literal(bullet()).withColor(ACCENT), entry.x() + PAD, entry.y() + 5, ACCENT);
            g.text(this.font, Component.literal(this.font.plainSubstrByWidth(entry.label().getString(),
                entry.w() - PAD * 2 - 10)), entry.x() + PAD + 10, entry.y() + 5, hover ? ACCENT : TEXT);
            tooltip(g, entry, mouseX, mouseY);
            end = entry.y() + ENTRY;
        }
        int y = end + 10;
        for (FormattedCharSequence line : this.font.split(sidebarHint(), this.sidebarW - PAD * 2)) {
            if (y + 9 > this.bottom - 4) {
                break;
            }
            g.text(this.font, line, this.sidebarX + PAD, y, DISABLED);
            y += 10;
        }
    }

    /** One more wrapped paragraph for a panel, each of its lines with the paragraph's colour. */
    protected void add(List<FormattedCharSequence> lines, List<Integer> colors, Component text, int color, int wrap) {
        for (FormattedCharSequence line : this.font.split(text, wrap)) {
            lines.add(line);
            colors.add(color);
        }
    }

    protected void tooltip(GuiGraphicsExtractor g, Action action, int mouseX, int mouseY) {
        if (action.tooltip() != null && inside(mouseX, mouseY, action.x(), action.y(), action.w(), action.h())) {
            g.setTooltipForNextFrame(this.font.split(action.tooltip(), 200), mouseX, mouseY);
        }
    }
}
