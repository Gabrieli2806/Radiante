package com.g2806.radiante.client.gui;

import com.g2806.radiante.mixin.gui.OptionInstanceAccessor;
import com.mojang.blaze3d.platform.InputConstants;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.function.Consumer;
import net.minecraft.ChatFormatting;
import net.minecraft.client.Minecraft;
import net.minecraft.client.OptionInstance;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.components.Tooltip;
import net.minecraft.client.gui.navigation.ScreenDirection;
import net.minecraft.client.gui.navigation.ScreenRectangle;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.renderer.RenderPipelines;
import net.minecraft.network.chat.Component;
import net.minecraft.network.chat.contents.TranslatableContents;
import net.minecraft.resources.Identifier;
import net.minecraft.util.FormattedCharSequence;

/**
 * The Radiante settings screen, after Sodium's own: a search field along the top; on the left the mod's name and one
 * entry per section, the one in view marked with a bar; on the right every section in one scrolling list, each under
 * its own heading, options as flat rows with the name on the left and the value (or a checkbox) on the right. Beside
 * the list, the description and cost of the option under the mouse. Actions (Reset, Advanced, Undo, Apply, Done) are
 * flat text buttons along the bottom.
 *
 * <p>Rows are drawn and handled here rather than as vanilla widgets: a click on a toggle flips it, on a list of values
 * steps to the next one (right click: the previous one), and a slider follows the mouse while held.
 *
 * <p>Pictures, all optional ({@code assets/radiante/textures/gui/settings/}, from the mod or a resource pack):
 * {@code icon.png} (32 x 32) beside the mod's name, and {@code <section>.png} (16 x 16) beside a section's name.
 */
final class SettingsLayout {

    /** A section and its options, in order; then its blocks, each under a heading of its own (the addons). */
    record Section(RadianteOptionsScreen.Category category, List<OptionInstance<?>> options, List<Group> groups,
                   List<Block> blocks) {
        Section(RadianteOptionsScreen.Category category, List<OptionInstance<?>> options, List<Group> groups) {
            this(category, options, groups, List.of());
        }
    }

    /**
     * Options under a heading that folds them away, as each addon's are in the Addons section; {@code key} (the
     * addon's id) is what its open or folded state is remembered by. {@code empty} is shown under the heading when
     * there are no options; a search that finds the heading shows all of them. {@code icon} (the addon's, null for
     * none) is drawn left of the heading.
     */
    record Block(String key, Component heading, AddonIcons.Icon icon, List<OptionInstance<?>> options,
                 Component empty) {
    }

    /** Advanced options about one thing, under a heading that folds them away. */
    record Group(String key, List<OptionInstance<?>> options) {
    }

    /**
     * What the buttons do. {@code advanced} is whether the Advanced options show; the toggle flips it.
     * {@code shaderPacks} opens the shader pack list (null for no button).
     */
    record Actions(Runnable reset, Runnable undo, Runnable apply, Runnable done, Extra extra, Runnable share,
                   boolean advanced, Runnable toggleAdvanced, Runnable rebuild, Runnable shaderPacks) {
    }

    /**
     * A button that belongs to one option, beside Advanced: installing or deleting an upscaler. It shows once the
     * option with the caption {@code anchorKey} was clicked, or always when that is null. Null for no button.
     */
    record Extra(Component label, Component tooltip, Runnable action, boolean enabled, String anchorKey) {
    }

    /**
     * The caption key of the option clicked last (or {@link #GROUP_FOCUS} and a group's key for a group's heading
     * reached with the keys); kept while the screen is rebuilt, which a click often causes.
     */
    private static String focusedKey;
    /**
     * The bottom button the keys are on (see {@link #footer()}), or -1 while they are on the list. The option in
     * {@link #focusedKey} stays as it was meanwhile, so a button that belongs to it (the upscaler's) stays too.
     */
    private static int focusedButton = -1;
    /** Advanced groups folded open, by key; kept while the game runs. */
    private static final java.util.Set<String> openGroups = new java.util.HashSet<>();
    private static final String GROUP_FOCUS = "#group:";
    private static final String BLOCK_FOCUS = "#block:";

    /**
     * Addon blocks the player opened (true) or folded (false), by key; saved in Radiante's options so they stay that
     * way after a restart. A block never touched is open when it is the only one and folded when there are more, so
     * one addon shows its options straight away and several make a short list of names.
     */
    private static java.util.Map<String, Boolean> blockStates;

    private static java.util.Map<String, Boolean> blockStates() {
        if (blockStates == null) {
            blockStates = new java.util.LinkedHashMap<>();
            for (String entry : com.g2806.radiante.client.option.Options.addonsOpen.split(",")) {
                int at = entry.indexOf('=');
                if (at > 0) {
                    blockStates.put(entry.substring(0, at).trim(), "1".equals(entry.substring(at + 1).trim()));
                }
            }
        }
        return blockStates;
    }

    private static boolean blockOpen(Section section, Block block) {
        Boolean state = blockStates().get(block.key());
        return state != null ? state : section.blocks().size() == 1;
    }

    private static void setBlockOpen(String key, boolean open) {
        blockStates().put(key, open);
        StringBuilder text = new StringBuilder();
        blockStates().forEach((id, state) -> text.append(text.isEmpty() ? "" : ",").append(id).append('=')
            .append(state ? '1' : '0'));
        com.g2806.radiante.client.option.Options.addonsOpen = text.toString();
        try {
            com.g2806.radiante.client.option.Options.overwriteConfig();
        } catch (RuntimeException e) {
            // Only where the blocks stand is lost, and only after a restart; the screen goes on.
        }
    }

    /**
     * A newly opened settings screen starts with no option in focus. One made by the settings screen itself (laid out
     * again after a change, Apply, Undo, Advanced) keeps it: with the keys or a controller the focus would otherwise
     * jump back to the top after every change that rebuilds the screen.
     */
    static void clearFocus() {
        if (Minecraft.getInstance().gui.screen() instanceof RadianteOptionsScreen) {
            return;
        }
        focusedKey = null;
        focusedButton = -1;
    }

    private static final int ACCENT = 0xFF8FE3C8;
    private static final int TEXT = 0xFFFFFFFF;
    private static final int TEXT_DIM = 0xFFB0B8B4;
    private static final int PANEL = 0xC0101212;
    private static final int PANEL_DARK = 0xD0060707;
    private static final int ROW_HOVER = 0x30FFFFFF;
    private static final int GROUP = 0x60000000;
    /** An option that does nothing as things are set. */
    private static final int TEXT_BLOCKED = 0xFF6A716E;
    private static final int NOTICE = 0xFFF0B040;

    private static final int ROW = 20;
    private static final int HEADER = 22;
    private static final int GAP = 6;
    private static final int PAD = 8;
    /** The side of the square an addon's icon is drawn in, beside its heading. */
    private static final int ICON = 16;

    private final Font font;
    private final int width;
    private final int height;
    private final List<Section> sections;
    private final Actions actions;
    private final Consumer<Double> onScroll;
    private EditBox search;

    // Laid out in init.
    private int sidebarX;
    private int sidebarW;
    private int listX;
    private int listW;
    private int top;
    private int bottom;
    private int panelX;
    private int panelW;
    private double scroll;
    private int contentHeight;

    /**
     * One line of the list: an option (advanced when it comes from a group), a group's heading, a block's heading
     * (when {@code block} is set), a note, or with none of these a section's heading.
     */
    private record Line(Section section, OptionInstance<?> option, Group group, Component note, boolean advanced,
                        int y, int height, Block block) {
        Line(Section section, OptionInstance<?> option, Group group, Component note, boolean advanced, int y,
             int height) {
            this(section, option, group, note, advanced, y, height, null);
        }

        boolean sectionHeading() {
            return this.option == null && this.group == null && this.note == null && this.block == null;
        }

        boolean heading() {
            return this.block != null;
        }
    }

    private final List<Line> lines = new ArrayList<>();
    private final List<int[]> sectionSpans = new ArrayList<>();
    private OptionInstance<?> described;
    private OptionInstance<Integer> dragging;
    private int dragX;
    private int dragWidth;

    private record FlatButton(Component label, Runnable action, boolean enabled, Component tooltip, int x, int y,
                              int w, Component ctrlLabel, Runnable ctrlAction, Component ctrlTooltip) {
        FlatButton(Component label, Runnable action, boolean enabled, Component tooltip, int x, int y, int w) {
            this(label, action, enabled, tooltip, x, y, w, null, null, null);
        }

        /** Held Ctrl turns a button with a second action (Reset to Defaults: Rebuild Shaders) into that one. */
        private boolean alternate() {
            return this.ctrlAction != null && Minecraft.getInstance().hasControlDown();
        }

        Component shownLabel() {
            return alternate() ? this.ctrlLabel : this.label;
        }

        Runnable shownAction() {
            return alternate() ? this.ctrlAction : this.action;
        }

        Component shownTooltip() {
            return alternate() ? this.ctrlTooltip : this.tooltip;
        }
    }

    private final List<FlatButton> buttons = new ArrayList<>();

    /** Which options take effect only after a restart, and whether their current choice still needs one. */
    interface RestartInfo {
        boolean needsRestart(OptionInstance<?> option);

        boolean pending(OptionInstance<?> option);
    }

    private RestartInfo restart = new RestartInfo() {
        @Override
        public boolean needsRestart(OptionInstance<?> option) {
            return false;
        }

        @Override
        public boolean pending(OptionInstance<?> option) {
            return false;
        }
    };

    void restartInfo(RestartInfo info) {
        this.restart = info;
    }

    /** Which options do nothing as things are set, and which other options an option decides about. */
    interface Availability {
        /** Why the option has no effect right now, or null when it has. */
        Component blocked(String optionKey);

        /** The options this one switches off or takes the place of; empty for none. */
        List<Component> affects(String optionKey);

        /** Something to know before changing the option, or null. */
        default Component notice(String optionKey) {
            return null;
        }
    }

    private Availability availability = new Availability() {
        @Override
        public Component blocked(String optionKey) {
            return null;
        }

        @Override
        public List<Component> affects(String optionKey) {
            return List.of();
        }
    };

    void availability(Availability info) {
        this.availability = info;
    }

    private boolean blocked(OptionInstance<?> option) {
        return this.availability.blocked(keyOf(caption(option))) != null;
    }

    SettingsLayout(Font font, int width, int height, List<Section> sections, Actions actions, Consumer<Double> onScroll) {
        this.font = font;
        this.width = width;
        this.height = height;
        this.sections = sections;
        this.actions = actions;
        this.onScroll = onScroll;
    }

    double scroll() {
        return this.scroll;
    }

    String searchText() {
        return this.search == null ? "" : this.search.getValue();
    }

    /** Lays the screen out; the search field is handed back to be added to the screen. */
    EditBox init(double initialScroll, String initialSearch) {
        if (this.sections.stream().noneMatch(section -> section.category() == selected) && !this.sections.isEmpty()) {
            selected = this.sections.get(0).category();
        }
        int margin = Math.max(8, this.width / 24);
        this.top = 40;
        this.bottom = this.height - 30;
        this.sidebarX = margin;
        this.sidebarW = Math.max(90, Math.min(150, this.width / 5));
        this.listX = this.sidebarX + this.sidebarW + GAP;
        int remaining = this.width - margin - this.listX;
        this.panelW = remaining > 460 ? Math.min(200, remaining / 3) : 0;
        this.listW = remaining - (this.panelW > 0 ? this.panelW + GAP : 0);
        this.panelX = this.listX + this.listW + GAP;

        // Share sits at the end of the search bar, out of the way of the settings and their buttons, and Shader Packs
        // beside it: both open screens of their own rather than change a setting.
        FlatButton share = flat("options.radiante.share", "options.radiante.share.tooltip", this.actions.share(), true);
        int shareW = this.font.width(share.label()) + 16;
        this.buttons.add(new FlatButton(share.label(), share.action(), true, share.tooltip(),
            this.width - margin - shareW, 13, shareW));
        int topRight = shareW;
        if (this.actions.shaderPacks() != null) {
            FlatButton packs = flat("options.radiante.shader_packs", "options.radiante.shader_packs.tooltip",
                this.actions.shaderPacks(), true);
            int packsW = this.font.width(packs.label()) + 16;
            this.buttons.add(new FlatButton(packs.label(), packs.action(), true, packs.tooltip(),
                this.width - margin - shareW - 4 - packsW, 13, packsW));
            topRight += packsW + 4;
        }
        this.search = new EditBox(this.font, this.sidebarX, 12, this.width - margin * 2 - topRight - 4, 18,
            Component.translatable("options.radiante.search"));
        this.search.setHint(Component.translatable("options.radiante.search").withStyle(ChatFormatting.DARK_GRAY));
        this.search.setValue(initialSearch);
        this.search.setResponder(text -> {
            this.scroll = 0.0;
            layoutLines();
        });

        layoutLines();
        this.scroll = clampScroll(initialScroll);

        this.margin = margin;
        this.topButtons = this.buttons.size();
        layoutButtons();
        return this.search;
    }

    private int margin;
    private int topButtons;

    /** The buttons along the bottom; laid out again when the option in focus changes, as one of them follows it. */
    private void layoutButtons() {
        while (this.buttons.size() > this.topButtons) {
            this.buttons.remove(this.buttons.size() - 1);
        }
        int margin = this.margin;
        int y = this.height - 24;
        int left = this.sidebarX;
        List<FlatButton> leftButtons = new ArrayList<>();
        FlatButton reset = flat("options.radiante.reset_defaults", "options.radiante.reset_defaults.tooltip",
            this.actions.reset(), true);
        leftButtons.add(new FlatButton(reset.label(), reset.action(), true, reset.tooltip(), 0, 0, 0,
            Component.translatable("options.radiante.rebuild_shaders"), this.actions.rebuild(),
            Component.translatable("options.radiante.rebuild_shaders.tooltip")));
        leftButtons.add(new FlatButton(Component.translatable(this.actions.advanced() ? "options.radiante.advanced.on"
            : "options.radiante.advanced.off"), this.actions.toggleAdvanced(), true,
            Component.translatable("options.radiante.advanced.tooltip"), 0, 0, 0));
        Extra extra = this.actions.extra();
        // Installing or deleting an upscaler belongs with the pipeline it serves: shown on the Quality section only.
        if (extra != null && selected == RadianteOptionsScreen.Category.QUALITY
            && (extra.anchorKey() == null || extra.anchorKey().equals(focusedKey))) {
            leftButtons.add(new FlatButton(extra.label(), extra.action(), extra.enabled(), extra.tooltip(), 0, 0, 0));
        }
        for (FlatButton button : leftButtons) {
            int w = this.font.width(button.label()) + 16;
            if (button.ctrlLabel() != null) {
                w = Math.max(w, this.font.width(button.ctrlLabel()) + 16);
            }
            this.buttons.add(new FlatButton(button.label(), button.action(), button.enabled(), button.tooltip(), left, y,
                w, button.ctrlLabel(), button.ctrlAction(), button.ctrlTooltip()));
            left += w + 4;
        }
        // With a controller in use, the buttons it has a button of its own for show that button's picture; left
        // out where they would run into the buttons on the left.
        List<FlatButton> right = rightButtons(y, true);
        if (!right.isEmpty() && right.get(right.size() - 1).x() < left) {
            right = rightButtons(y, false);
        }
        this.buttons.addAll(right);
    }

    /** Done, Apply and Undo from the right edge leftwards, with or without the controller's pictures. */
    private List<FlatButton> rightButtons(int y, boolean glyphs) {
        List<FlatButton> right = new ArrayList<>();
        int x = this.width - this.margin;
        for (FlatButton button : List.of(
            flat("gui.done", null, this.actions.done(), true, glyphs ? SettingsControls.Hint.BACK : null),
            flat("options.radiante.apply", "options.radiante.apply.tooltip", this.actions.apply(), true,
                glyphs ? SettingsControls.Hint.APPLY : null),
            flat("options.radiante.undo", "options.radiante.undo.tooltip", this.actions.undo(), true,
                glyphs ? SettingsControls.Hint.UNDO : null))) {
            int w = this.font.width(button.label()) + 16;
            x -= w;
            right.add(new FlatButton(button.label(), button.action(), true, button.tooltip(), x, y, w));
            x -= 4;
        }
        return right;
    }

    private static FlatButton flat(String key, String tooltip, Runnable action, boolean enabled) {
        return new FlatButton(Component.translatable(key), action, enabled,
            tooltip == null ? null : Component.translatable(tooltip), 0, 0, 0);
    }

    private static FlatButton flat(String key, String tooltip, Runnable action, boolean enabled,
        SettingsControls.Hint hint) {
        Component label = Component.translatable(key);
        return new FlatButton(hint == null ? label : SettingsControls.withGlyph(hint, label), action, enabled,
            tooltip == null ? null : Component.translatable(tooltip), 0, 0, 0);
    }

    /** Puts every section and option that matches the search one under the other. */
    private void layoutLines() {
        this.lines.clear();
        this.sectionSpans.clear();
        String filter = this.search == null ? "" : this.search.getValue().trim().toLowerCase(Locale.ROOT);
        int y = 0;
        for (Section section : this.sections) {
            // One section at a time, as tabs; a search looks through all of them.
            if (filter.isEmpty() && section.category() != selected) {
                continue;
            }
            List<OptionInstance<?>> shown = matching(section.options(), filter);
            // Advanced options: under their group's heading when Advanced is on (a search opens the groups it finds
            // something in); when it is off a search only says how many more it would find there.
            List<Group> groups = new ArrayList<>();
            int hidden = 0;
            for (Group group : section.groups()) {
                List<OptionInstance<?>> matches = matching(group.options(), filter);
                if (matches.isEmpty()) {
                    continue;
                }
                if (this.actions.advanced()) {
                    groups.add(new Group(group.key(), matches));
                } else if (!filter.isEmpty()) {
                    hidden += matches.size();
                }
            }
            List<Block> blocks = new ArrayList<>();
            for (Block block : section.blocks()) {
                boolean named = !filter.isEmpty()
                    && block.heading().getString().toLowerCase(Locale.ROOT).contains(filter);
                List<OptionInstance<?>> matches = named ? block.options() : matching(block.options(), filter);
                if (filter.isEmpty() || named || !matches.isEmpty()) {
                    blocks.add(new Block(block.key(), block.heading(), block.icon(), matches, block.empty()));
                }
            }
            if (shown.isEmpty() && groups.isEmpty() && hidden == 0 && blocks.isEmpty()) {
                continue;
            }
            int start = y;
            this.lines.add(new Line(section, null, null, null, false, y, HEADER));
            y += HEADER + 2;
            for (OptionInstance<?> option : shown) {
                this.lines.add(new Line(section, option, null, null, false, y, ROW));
                y += ROW;
            }
            for (Block block : blocks) {
                this.lines.add(new Line(section, null, null, null, false, y, ROW, block));
                y += ROW;
                // A search shows what it found in every block, folded or not.
                if (filter.isEmpty() && !blockOpen(section, block)) {
                    continue;
                }
                for (OptionInstance<?> option : block.options()) {
                    this.lines.add(new Line(section, option, null, null, false, y, ROW));
                    y += ROW;
                }
                if (block.options().isEmpty() && block.empty() != null) {
                    this.lines.add(new Line(section, null, null, block.empty(), false, y, ROW));
                    y += ROW;
                }
            }
            for (Group group : groups) {
                this.lines.add(new Line(section, null, group, null, false, y, ROW));
                y += ROW;
                if (!filter.isEmpty() || openGroups.contains(group.key())) {
                    for (OptionInstance<?> option : group.options()) {
                        this.lines.add(new Line(section, option, group, null, true, y, ROW));
                        y += ROW;
                    }
                }
            }
            if (hidden > 0) {
                this.lines.add(new Line(section, null, null,
                    Component.translatable("options.radiante.advanced.more", hidden), false, y, ROW));
                y += ROW;
            }
            y += GAP;
            this.sectionSpans.add(new int[] {section.category().ordinal(), start, y});
        }
        this.contentHeight = y;
    }

    private static List<OptionInstance<?>> matching(List<OptionInstance<?>> options, String filter) {
        List<OptionInstance<?>> shown = new ArrayList<>();
        for (OptionInstance<?> option : options) {
            if (filter.isEmpty() || caption(option).getString().toLowerCase(Locale.ROOT).contains(filter)) {
                shown.add(option);
            }
        }
        return shown;
    }

    private double clampScroll(double value) {
        return Math.max(0.0, Math.min(value, Math.max(0, this.contentHeight - (this.bottom - this.top))));
    }

    // ---- drawing ----

    void extract(GuiGraphicsExtractor g, int mouseX, int mouseY) {
        // The mouse moving hands the description back to the row under it.
        if (mouseX != this.lastMouseX || mouseY != this.lastMouseY) {
            if (this.lastMouseX != Integer.MIN_VALUE) {
                keysLead = false;
            }
            this.lastMouseX = mouseX;
            this.lastMouseY = mouseY;
        }
        // Sidebar: the mod, then the sections.
        g.fill(this.sidebarX, this.top, this.sidebarX + this.sidebarW, this.bottom, PANEL);
        int headerY = this.top + 6;
        Identifier icon = texture("icon");
        int nameX = this.sidebarX + PAD;
        if (icon != null) {
            g.blit(RenderPipelines.GUI_TEXTURED, icon, nameX, headerY, 0.0f, 0.0f, 24, 24, 24, 24);
            nameX += 30;
        }
        g.text(this.font, Component.literal("Radiante").withStyle(ChatFormatting.BOLD), nameX, headerY + 3, TEXT);
        g.text(this.font, Component.translatable("options.radiante.subtitle"), nameX, headerY + 14, TEXT_DIM);
        int entryY = this.top + 38;
        RadianteOptionsScreen.Category inView = sectionInView();
        for (Section section : this.sections) {
            boolean selected = section.category() == inView;
            boolean hover = inside(mouseX, mouseY, this.sidebarX, entryY, this.sidebarW, 18);
            if (selected) {
                g.fill(this.sidebarX, entryY, this.sidebarX + this.sidebarW, entryY + 18, PANEL_DARK);
                g.fill(this.sidebarX + this.sidebarW - 3, entryY, this.sidebarX + this.sidebarW, entryY + 18, ACCENT);
            } else if (hover) {
                g.fill(this.sidebarX, entryY, this.sidebarX + this.sidebarW, entryY + 18, ROW_HOVER);
            }
            int textX = this.sidebarX + PAD;
            Identifier sectionIcon = texture(section.category().name().toLowerCase(Locale.ROOT));
            if (sectionIcon != null) {
                g.blit(RenderPipelines.GUI_TEXTURED, sectionIcon, textX, entryY + 1, 0.0f, 0.0f, 16, 16, 16, 16);
                textX += 20;
            }
            g.text(this.font, Component.translatable(section.category().key()), textX, entryY + 5,
                selected ? TEXT : ACCENT);
            entryY += 20;
        }
        // With a controller in use: the bumpers that move between the sections, under them.
        Component previous = SettingsControls.glyph(SettingsControls.Hint.PREVIOUS_SECTION);
        Component next = SettingsControls.glyph(SettingsControls.Hint.NEXT_SECTION);
        if (previous != null && next != null) {
            Component pair = Component.empty().append(previous).append(" ").append(next);
            Component hint = Component.empty().append(pair).append(" ")
                .append(Component.translatable("options.radiante.controller.sections"));
            g.text(this.font, this.font.width(hint) <= this.sidebarW - PAD * 2 ? hint : pair, this.sidebarX + PAD,
                entryY + 6, TEXT_DIM);
        }

        // The list.
        g.enableScissor(this.listX, this.top, this.listX + this.listW, this.bottom);
        OptionInstance<?> hovered = null;
        int base = this.top - (int) this.scroll;
        for (int i = 0; i < this.lines.size(); i++) {
            Line line = this.lines.get(i);
            int y = base + line.y();
            if (line.sectionHeading()) {
                if (y + HEADER > this.top && y < this.bottom) {
                    g.fill(this.listX, Math.max(y, this.top), this.listX + this.listW, Math.min(y + HEADER, this.bottom),
                        PANEL_DARK);
                }
                g.text(this.font, Component.literal("◆ ").append(Component.translatable(line.section().category().key())),
                    this.listX + PAD, y + 7, ACCENT);
                continue;
            }
        }
        for (int i = 0; i < this.lines.size(); i++) {
            Line line = this.lines.get(i);
            if (line.sectionHeading()) {
                continue;
            }
            int y = base + line.y();
            if (y + ROW < this.top || y > this.bottom) {
                continue;
            }
            // The group's dark box, a slice per row (the first one reaching up to the header): one tall box for the
            // whole group went missing once its header scrolled out of view.
            boolean firstOfSection = i > 0 && this.lines.get(i - 1).sectionHeading();
            int sliceTop = Math.max(firstOfSection ? y - 2 : y, this.top);
            int sliceBottom = Math.min(y + ROW, this.bottom);
            if (sliceBottom > sliceTop) {
                g.fill(this.listX + 4, sliceTop, this.listX + this.listW - 4, sliceBottom, GROUP);
            }
            boolean hover = inside(mouseX, mouseY, this.listX + 4, y, this.listW - 8, ROW)
                && mouseY >= this.top && mouseY < this.bottom;
            if (line.heading()) {
                // A block's heading (an addon's name): an arrow for folded or open as on the advanced groups, the
                // addon's icon fitted into a square left of the name, the name brighter than a note, how many
                // options it holds on the right, and a rule under it.
                boolean open = blockOpen(line.section(), line.block()) || !searchEmpty();
                if (hover) {
                    g.fill(this.listX + 4, y, this.listX + this.listW - 4, y + ROW, ROW_HOVER);
                }
                if (keyboardFocus && focusedButton < 0 && navKey(line).equals(focusedKey)) {
                    g.outline(this.listX + 4, y, this.listW - 8, ROW, ACCENT);
                }
                int textX = this.listX + PAD + 4;
                Component arrow = Component.literal(open ? "▾ " : "▸ ");
                g.text(this.font, arrow, textX, y + 6, ACCENT);
                textX += this.font.width(arrow);
                AddonIcons.Icon addonIcon = line.block().icon();
                if (addonIcon != null) {
                    int iw = addonIcon.width();
                    int ih = addonIcon.height();
                    int w = iw >= ih ? ICON : Math.max(1, ICON * iw / ih);
                    int h = ih >= iw ? ICON : Math.max(1, ICON * ih / iw);
                    g.blit(RenderPipelines.GUI_TEXTURED, addonIcon.texture(), textX + (ICON - w) / 2,
                        y + 1 + (ICON - h) / 2, 0.0f, 0.0f, w, h, iw, ih, iw, ih);
                    textX += ICON + 4;
                }
                g.text(this.font, line.block().heading(), textX, y + 6, hover ? ACCENT : TEXT);
                Component count = Component.literal(String.valueOf(line.block().options().size()));
                g.text(this.font, count, this.listX + this.listW - PAD - 4 - this.font.width(count), y + 6, TEXT_DIM);
                g.fill(this.listX + PAD + 4, y + ROW - 2, this.listX + this.listW - PAD - 4, y + ROW - 1, 0x60FFFFFF);
                continue;
            }
            if (line.note() != null) {
                g.text(this.font, line.note(), this.listX + PAD + 4, y + 6, TEXT_DIM);
                continue;
            }
            if (line.option() == null) {
                // A group's heading: an arrow for folded or open, and how many options it holds.
                boolean open = openGroups.contains(line.group().key()) || !searchEmpty();
                if (hover) {
                    g.fill(this.listX + 4, y, this.listX + this.listW - 4, y + ROW, ROW_HOVER);
                }
                if (keyboardFocus && focusedButton < 0 && navKey(line).equals(focusedKey)) {
                    g.outline(this.listX + 4, y, this.listW - 8, ROW, ACCENT);
                }
                g.text(this.font, Component.literal(open ? "▾ " : "▸ ").append(Component.translatable(
                    line.group().key())), this.listX + PAD + 4, y + 6, hover ? TEXT : ACCENT);
                Component count = Component.literal(String.valueOf(line.group().options().size()));
                g.text(this.font, count, this.listX + this.listW - PAD - 4 - this.font.width(count), y + 6, TEXT_DIM);
                continue;
            }
            if (hover) {
                g.fill(this.listX + 4, y, this.listX + this.listW - 4, y + ROW, ROW_HOVER);
                hovered = line.option();
            }
            drawRow(g, line.option(), y, hover, line.advanced());
        }
        g.disableScissor();
        if (this.contentHeight > this.bottom - this.top) {
            int view = this.bottom - this.top;
            int bar = Math.max(16, view * view / this.contentHeight);
            int barY = this.top + (int) ((view - bar) * this.scroll / (this.contentHeight - view));
            g.fill(this.listX + this.listW - 2, barY, this.listX + this.listW, barY + bar, ACCENT);
        }
        if (hovered != null && !keysLead) {
            this.described = hovered;
        }

        drawDescription(g, mouseX, mouseY, hovered);

        FlatButton keyed = focusedFooterButton();
        for (FlatButton button : this.buttons) {
            boolean hover = button.enabled() && (inside(mouseX, mouseY, button.x(), button.y(), button.w(), 16)
                || button == keyed);
            g.fill(button.x(), button.y(), button.x() + button.w(), button.y() + 16, hover ? PANEL_DARK : PANEL);
            if (button == keyed) {
                g.outline(button.x(), button.y(), button.w(), 16, ACCENT);
                if (keysLead && button.shownTooltip() != null) {
                    // Above the button, clear of it (vanilla's tooltip starts 12 above the point it is given).
                    List<FormattedCharSequence> tip = this.font.split(button.shownTooltip(), 200);
                    g.setTooltipForNextFrame(tip, button.x(), Math.max(12, button.y() - tip.size() * 10 - 6));
                }
            }
            if (hover) {
                g.fill(button.x(), button.y() + 15, button.x() + button.w(), button.y() + 16, ACCENT);
            }
            g.centeredText(this.font, button.shownLabel(), button.x() + button.w() / 2, button.y() + 4,
                button.enabled() ? (hover ? ACCENT : TEXT) : 0xFF606060);
            if (button.shownTooltip() != null && inside(mouseX, mouseY, button.x(), button.y(), button.w(), 16)) {
                g.setTooltipForNextFrame(this.font.split(button.shownTooltip(), 200), mouseX, mouseY);
            }
        }
    }

    private void drawRow(GuiGraphicsExtractor g, OptionInstance<?> option, int y, boolean hover, boolean advanced) {
        int right = this.listX + this.listW - PAD - 4;
        if (keyboardFocus && focusedButton < 0 && focusedKey != null && focusedKey.equals(focusKey(option))) {
            // The row the arrow keys work on.
            g.outline(this.listX + 4, y, this.listW - 8, ROW, ACCENT);
        }
        // Grey all over while another setting keeps it from doing anything; its description says which.
        boolean blocked = blocked(option);
        // Advanced options sit a step in and a shade dimmer than the everyday ones above them.
        g.text(this.font, caption(option), this.listX + PAD + (advanced ? 14 : 4), y + 6,
            blocked ? TEXT_BLOCKED : advanced ? TEXT_DIM : TEXT);
        if (this.restart.needsRestart(option)) {
            // Marked on the row itself, amber once the choice differs from what the running game uses.
            boolean pending = this.restart.pending(option);
            Component tag = Component.translatable(pending ? "options.radiante.restart.tag_pending"
                : "options.radiante.restart.tag");
            g.text(this.font, tag, this.listX + PAD + 12 + this.font.width(caption(option)), y + 6,
                pending ? 0xFFF0B040 : 0xFF808080);
        }
        Object value = option.get();
        if (isCheckbox(option) && value instanceof Boolean on) {
            int box = 10;
            int bx = right - box;
            int by = y + 5;
            g.outline(bx, by, box, box, blocked ? TEXT_BLOCKED : hover ? ACCENT : 0xFFD0D0D0);
            if (on) {
                g.fill(bx + 2, by + 2, bx + box - 2, by + box - 2, blocked ? TEXT_BLOCKED : ACCENT);
            }
            return;
        }
        Component text = valueText(option);
        int textWidth = this.font.width(text);
        if (option.values() instanceof OptionInstance.IntRange range) {
            // A thin track under the value, filled to where it stands.
            int trackW = Math.min(90, this.listW / 3);
            int tx = right - trackW;
            int ty = y + ROW - 4;
            int span = Math.max(1, range.maxInclusive() - range.minInclusive());
            int filled = (int) ((long) trackW * ((Integer) value - range.minInclusive()) / span);
            g.fill(tx, ty, tx + trackW, ty + 2, blocked ? 0x30FFFFFF : 0x60FFFFFF);
            g.fill(tx, ty, tx + filled, ty + 2, blocked ? TEXT_BLOCKED : ACCENT);
            g.fill(tx + filled - 1, ty - 2, tx + filled + 1, ty + 4, blocked ? TEXT_BLOCKED : TEXT);
        }
        g.text(this.font, text, right - textWidth, y + 6, blocked ? TEXT_BLOCKED : hover ? ACCENT : TEXT);
    }

    private void drawDescription(GuiGraphicsExtractor g, int mouseX, int mouseY, OptionInstance<?> hovered) {
        OptionInstance<?> option = this.panelW > 0 ? this.described : hovered;
        int tipX = mouseX;
        int tipY = mouseY;
        int keyedRowY = -1;
        if (this.panelW <= 0 && keysLead && focusedButton < 0) {
            // No panel beside the list (a narrow window or a large GUI scale): the description of the row the keys
            // or a controller are on goes under that row, since there may be no mouse pointer to follow.
            option = null;
            List<Line> rows = navRows();
            int index = focusedRow(rows);
            if (index >= 0 && rows.get(index).option() != null) {
                Line line = rows.get(index);
                int y = this.top - (int) this.scroll + line.y();
                if (y >= this.top && y + ROW <= this.bottom) {
                    option = line.option();
                    tipX = this.listX + this.listW / 3;
                    tipY = y + ROW + 14;
                    keyedRowY = y;
                }
            }
        }
        if (option == null) {
            return;
        }
        List<FormattedCharSequence> lines = new ArrayList<>();
        Tooltip tooltip = tooltipOf(option);
        int wrap = this.panelW > 0 ? this.panelW - PAD * 2 : 200;
        if (tooltip != null) {
            lines.addAll(tooltip.toCharSequence(Minecraft.getInstance()));
        }
        String optionKey = keyOf(caption(option));
        OptionImpact impact = OptionImpact.of(optionKey);
        if (this.restart.needsRestart(option)) {
            lines.addAll(this.font.split(Component.translatable("options.radiante.restart.hint")
                .withColor(NOTICE), wrap));
        }
        Component blockedBy = this.availability.blocked(optionKey);
        if (blockedBy != null) {
            lines.addAll(this.font.split(blockedBy.copy().withColor(NOTICE), wrap));
        }
        Component notice = this.availability.notice(optionKey);
        if (notice != null) {
            lines.addAll(this.font.split(notice.copy().withColor(0xFF9FC8FF), wrap));
        }
        List<Component> affects = this.availability.affects(optionKey);
        if (!affects.isEmpty()) {
            net.minecraft.network.chat.MutableComponent names = Component.empty();
            for (int i = 0; i < affects.size(); i++) {
                names.append(i == 0 ? "" : ", ").append(affects.get(i));
            }
            lines.addAll(this.font.split(Component.translatable("options.radiante.affects", names)
                .withColor(ACCENT), wrap));
        }
        if (this.panelW <= 0) {
            List<FormattedCharSequence> all = new ArrayList<>(lines);
            if (impact != null) {
                all.addAll(this.font.split(impactLine(impact), wrap));
            }
            if (!all.isEmpty()) {
                // Under the focused row, or above it when it does not fit below (vanilla's tooltip starts 12 above
                // the point it is given and is pushed up off the bottom edge, over the row).
                int tall = all.size() * 10 + 8;
                if (keyedRowY >= 0 && keyedRowY + ROW + 2 + tall > this.height) {
                    tipY = Math.max(12, keyedRowY - tall + 10);
                }
                g.setTooltipForNextFrame(all, tipX, tipY);
            }
            return;
        }
        g.fill(this.panelX, this.top, this.panelX + this.panelW, this.bottom, PANEL);
        int x = this.panelX + PAD;
        int y = this.top + PAD;
        for (FormattedCharSequence line : this.font.split(caption(option).copy().withStyle(ChatFormatting.BOLD), wrap)) {
            g.text(this.font, line, x, y, ACCENT);
            y += 11;
        }
        y += 4;
        for (FormattedCharSequence line : lines) {
            g.text(this.font, line, x, y, TEXT_DIM);
            y += 10;
        }
        if (impact != null) {
            y += 8;
            for (FormattedCharSequence line : this.font.split(impactLine(impact), wrap)) {
                g.text(this.font, line, x, y, TEXT);
                y += 10;
            }
        }
    }

    private static Component impactLine(OptionImpact impact) {
        return Component.translatable("options.radiante.impact",
            Component.translatable(impact.key).withColor(impact.color));
    }

    // ---- input ----

    /** The click vanilla buttons make, for this screen's own drawn buttons and rows. */
    private static void clickSound() {
        net.minecraft.client.gui.components.AbstractWidget.playButtonClickSound(
            net.minecraft.client.Minecraft.getInstance().getSoundManager());
    }

    boolean mouseClicked(double mouseX, double mouseY, int button) {
        keyboardFocus = false;
        keysLead = false;
        focusedButton = -1;
        for (FlatButton flat : this.buttons) {
            if (flat.enabled() && inside(mouseX, mouseY, flat.x(), flat.y(), flat.w(), 16)) {
                clickSound();
                flat.shownAction().run();
                return true;
            }
        }
        // Sidebar: jump to the section.
        int entryY = this.top + 38;
        for (Section section : this.sections) {
            if (inside(mouseX, mouseY, this.sidebarX, entryY, this.sidebarW, 18)) {
                if (selected != section.category()) {
                    clickSound();
                    selected = section.category();
                    this.scroll = 0.0;
                    this.onScroll.accept(this.scroll);
                    if (this.search != null) {
                        this.search.setValue("");
                    }
                    layoutLines();
                    layoutButtons();
                }
                return true;
            }
            entryY += 20;
        }
        if (mouseY < this.top || mouseY >= this.bottom || mouseX < this.listX || mouseX >= this.listX + this.listW) {
            return false;
        }
        int base = this.top - (int) this.scroll;
        for (Line line : this.lines) {
            if (line.sectionHeading() || line.note() != null) {
                continue;
            }
            int y = base + line.y();
            if (mouseY >= y && mouseY < y + ROW) {
                if (line.heading()) {
                    toggleBlock(line);
                    return true;
                }
                if (line.option() == null) {
                    toggleGroup(line.group());
                    return true;
                }
                if (!(line.option().values() instanceof OptionInstance.IntRange) && !blocked(line.option())) {
                    clickSound();
                }
                // Laid out before the click takes effect: it may replace this screen with a new one.
                focusedKey = focusKey(line.option());
                layoutButtons();
                // Not while it does nothing: the row is grey and its description says what to change first.
                if (!blocked(line.option())) {
                    activate(line.option(), mouseX, button);
                }
                return true;
            }
        }
        return false;
    }

    @SuppressWarnings("unchecked")
    private void activate(OptionInstance<?> option, double mouseX, int button) {
        Object value = option.get();
        if (value instanceof Boolean on) {
            ((OptionInstance<Boolean>) option).set(!on);
        } else if (option.values() instanceof OptionInstance.IntRange) {
            int trackW = Math.min(90, this.listW / 3);
            this.dragging = (OptionInstance<Integer>) option;
            this.dragWidth = trackW;
            this.dragX = this.listX + this.listW - PAD - 4 - trackW;
            dragTo(mouseX);
        } else if (option.values() instanceof OptionInstance.Enum<?> values) {
            List<Object> list = (List<Object>) values.valueListSupplier().getDefaultList();
            int index = list.indexOf(value);
            int next = Math.floorMod(index + (button == InputConstants.MOUSE_BUTTON_RIGHT ? -1 : 1), list.size());
            ((OptionInstance<Object>) option).set(list.get(next));
        }
    }

    boolean mouseDragged(double mouseX) {
        if (this.dragging == null) {
            return false;
        }
        dragTo(mouseX);
        return true;
    }

    void mouseReleased() {
        this.dragging = null;
    }

    private void dragTo(double mouseX) {
        OptionInstance.IntRange range = (OptionInstance.IntRange) this.dragging.values();
        double t = Math.max(0.0, Math.min(1.0, (mouseX - this.dragX) / this.dragWidth));
        int value = range.minInclusive() + (int) Math.round(t * (range.maxInclusive() - range.minInclusive()));
        if (value != this.dragging.get()) {
            this.dragging.set(value);
        }
    }

    /** Set while the keys (or a controller) move through the list: the row in focus is outlined. */
    private static boolean keyboardFocus;
    /** The keys were used after the mouse last moved: the description follows the focused row, not the mouse. */
    private static boolean keysLead;
    private int lastMouseX = Integer.MIN_VALUE;
    private int lastMouseY;

    private static void keysInUse() {
        keyboardFocus = true;
        keysLead = true;
    }

    /**
     * Keyboard: Up and Down move through the options and the folded groups' headings, on past the last one to the
     * buttons along the bottom; Left and Right step a slider by one (ten with Shift) or a list of values to its
     * neighbour, fold a group shut or open, or move between the buttons; Enter or Space flips a switch, steps a list,
     * folds a group or presses a button; Tab moves to the next section (Shift: the previous). Without a row picked
     * yet, the keys work on the one under the mouse. True when used; Up on the first row is not, so the screen's own
     * navigation moves on to the search field.
     *
     * <p>Minecraft's input codes changed with the move to SDL, so the keys are read through the event's own tests
     * (isUp, isSelection...) rather than by number. Controller mods that press keys for the buttons reach this too.
     */
    boolean keyPressed(KeyEvent event) {
        if (event.isCycleFocus()) {
            section(event.hasShiftDown() ? -1 : 1);
            return true;
        }
        if (event.isUp()) {
            return navigate(ScreenDirection.UP, 1);
        }
        if (event.isDown()) {
            return navigate(ScreenDirection.DOWN, 1);
        }
        if (event.isLeft() || event.isRight()) {
            return navigate(event.isLeft() ? ScreenDirection.LEFT : ScreenDirection.RIGHT,
                event.hasShiftDown() ? 10 : 1);
        }
        if (event.isSelection()) {
            return activateFocused();
        }
        return false;
    }

    /** The next section (or with -1 the previous one), as Tab does. */
    void section(int direction) {
        // Through the sections shown: Addons only is one when an addon is installed.
        int at = 0;
        for (int i = 0; i < this.sections.size(); i++) {
            if (this.sections.get(i).category() == selected) {
                at = i;
            }
        }
        selected = this.sections.get(Math.floorMod(at + direction, this.sections.size())).category();
        this.scroll = 0.0;
        this.onScroll.accept(this.scroll);
        if (this.search != null) {
            this.search.setValue("");
        }
        layoutLines();
        focusedButton = -1;
        // With the keys or a controller in use, the outline goes to the new section's first row.
        List<Line> rows = navRows();
        if (keyboardFocus && !rows.isEmpty()) {
            focusRow(rows.get(0));
        }
        layoutButtons();
        clickSound();
    }

    /** The rows the keys stop on: options, and the headings of the advanced groups and of the addons. */
    private List<Line> navRows() {
        List<Line> rows = new ArrayList<>();
        for (Line line : this.lines) {
            if (line.option() != null || line.group() != null || line.heading()) {
                rows.add(line);
            }
        }
        return rows;
    }

    private static String navKey(Line line) {
        return line.option() != null ? focusKey(line.option())
            : line.heading() ? BLOCK_FOCUS + line.block().key() : GROUP_FOCUS + line.group().key();
    }

    /** The row in focus, or else the one under the mouse; -1 for none. */
    private int focusedRow(List<Line> rows) {
        for (int i = 0; i < rows.size(); i++) {
            if (focusedKey != null && focusedKey.equals(navKey(rows.get(i)))) {
                return i;
            }
        }
        if (this.described != null) {
            for (int i = 0; i < rows.size(); i++) {
                if (rows.get(i).option() == this.described) {
                    return i;
                }
            }
        }
        return -1;
    }

    /**
     * The buttons in the order Left and Right go through them: the bottom ones from left to right, then Share up by
     * the search field.
     */
    private List<FlatButton> footer() {
        List<FlatButton> bottom = new ArrayList<>(this.buttons.subList(this.topButtons, this.buttons.size()));
        bottom.sort(java.util.Comparator.comparingInt(FlatButton::x));
        List<FlatButton> footer = new ArrayList<>();
        for (FlatButton button : bottom) {
            if (button.enabled()) {
                footer.add(button);
            }
        }
        footer.addAll(this.buttons.subList(0, this.topButtons));
        return footer;
    }

    private FlatButton focusedFooterButton() {
        if (!keyboardFocus || focusedButton < 0) {
            return null;
        }
        List<FlatButton> footer = footer();
        return footer.isEmpty() ? null : footer.get(Math.min(focusedButton, footer.size() - 1));
    }

    private void focusRow(Line line) {
        focusedKey = navKey(line);
        focusedButton = -1;
        if (line.option() != null) {
            this.described = line.option();
        }
        keysInUse();
        // Into view.
        int view = this.bottom - this.top;
        if (line.y() < this.scroll + HEADER) {
            this.scroll = clampScroll(line.y() - HEADER);
        } else if (line.y() + ROW > this.scroll + view) {
            this.scroll = clampScroll(line.y() + ROW - view);
        }
        this.onScroll.accept(this.scroll);
        layoutButtons();
    }

    /**
     * One step in a direction, as the arrow keys and a controller's D-pad or stick make it: Up and Down move the
     * focus, Left and Right change the focused value by {@code amount} steps (or move along the buttons). True when
     * something happened; false at the top of the list, where the focus may leave it.
     */
    @SuppressWarnings("unchecked")
    boolean navigate(ScreenDirection direction, int amount) {
        List<Line> rows = navRows();
        if (focusedButton >= 0 && keyboardFocus) {
            List<FlatButton> footer = footer();
            switch (direction) {
                case UP -> {
                    if (rows.isEmpty()) {
                        return false;
                    }
                    int index = focusedRow(rows);
                    focusRow(rows.get(index < 0 ? rows.size() - 1 : index));
                }
                case LEFT, RIGHT -> {
                    int next = Math.max(0, Math.min(footer.size() - 1,
                        focusedButton + (direction == ScreenDirection.LEFT ? -1 : 1)));
                    focusedButton = next;
                }
                default -> {
                }
            }
            return true;
        }
        int index = focusedRow(rows);
        if (direction == ScreenDirection.UP || direction == ScreenDirection.DOWN) {
            if (rows.isEmpty()) {
                if (direction == ScreenDirection.DOWN && !footer().isEmpty()) {
                    focusedButton = 0;
                    keysInUse();
                    return true;
                }
                return false;
            }
            if (index < 0 || !keyboardFocus) {
                // The first step only shows where the keys are.
                focusRow(rows.get(Math.max(0, index)));
                return true;
            }
            if (direction == ScreenDirection.UP) {
                if (index == 0) {
                    return false;
                }
                focusRow(rows.get(index - 1));
                return true;
            }
            if (index == rows.size() - 1) {
                // On past the last row to the buttons along the bottom.
                if (!footer().isEmpty()) {
                    focusedButton = 0;
                }
                return true;
            }
            focusRow(rows.get(index + 1));
            return true;
        }
        if (index < 0) {
            return false;
        }
        Line line = rows.get(index);
        focusedKey = navKey(line);
        keysInUse();
        int sign = direction == ScreenDirection.LEFT ? -1 : 1;
        if (line.heading()) {
            // An addon's heading: Right opens it, Left folds it (not while a search shows it open anyway).
            if (searchEmpty() && blockOpen(line.section(), line.block()) != (sign > 0)) {
                toggleBlock(line);
            }
            return true;
        }
        if (line.option() == null) {
            // A group's heading: Right opens it, Left folds it.
            boolean open = openGroups.contains(line.group().key());
            if (open != (sign > 0)) {
                toggleGroup(line.group());
            }
            return true;
        }
        OptionInstance<?> option = line.option();
        this.described = option;
        if (blocked(option)) {
            return true;
        }
        Object value = option.get();
        if (value instanceof Boolean on) {
            ((OptionInstance<Boolean>) option).set(!on);
            clickSound();
        } else if (option.values() instanceof OptionInstance.IntRange range) {
            int next = Math.max(range.minInclusive(), Math.min(range.maxInclusive(), (Integer) value + sign * amount));
            if (next != (Integer) value) {
                ((OptionInstance<Integer>) option).set(next);
            }
        } else if (option.values() instanceof OptionInstance.Enum<?> values) {
            List<Object> list = (List<Object>) values.valueListSupplier().getDefaultList();
            int at = list.indexOf(value);
            ((OptionInstance<Object>) option).set(list.get(Math.floorMod(at + sign, list.size())));
            clickSound();
        }
        layoutButtons();
        return true;
    }

    /** Enter, Space or a controller's A: flips the focused switch, steps its list, folds its group or presses it. */
    @SuppressWarnings("unchecked")
    boolean activateFocused() {
        FlatButton button = focusedFooterButton();
        if (button != null) {
            clickSound();
            button.shownAction().run();
            return true;
        }
        List<Line> rows = navRows();
        int index = focusedRow(rows);
        if (index < 0) {
            return false;
        }
        Line line = rows.get(index);
        focusedKey = navKey(line);
        keysInUse();
        if (line.heading()) {
            toggleBlock(line);
            return true;
        }
        if (line.option() == null) {
            toggleGroup(line.group());
            return true;
        }
        OptionInstance<?> option = line.option();
        this.described = option;
        if (blocked(option) || option.values() instanceof OptionInstance.IntRange) {
            return true;
        }
        Object value = option.get();
        if (value instanceof Boolean on) {
            ((OptionInstance<Boolean>) option).set(!on);
        } else if (option.values() instanceof OptionInstance.Enum<?> values) {
            List<Object> list = (List<Object>) values.valueListSupplier().getDefaultList();
            ((OptionInstance<Object>) option).set(list.get(Math.floorMod(list.indexOf(value) + 1, list.size())));
        }
        clickSound();
        layoutButtons();
        return true;
    }

    private void toggleGroup(Group group) {
        clickSound();
        if (!openGroups.remove(group.key())) {
            openGroups.add(group.key());
        }
        relayout(GROUP_FOCUS + group.key());
    }

    /** Folds an addon's block or opens it. A search shows every block it finds open, so then it changes nothing. */
    private void toggleBlock(Line heading) {
        if (!searchEmpty()) {
            return;
        }
        clickSound();
        setBlockOpen(heading.block().key(), !blockOpen(heading.section(), heading.block()));
        relayout(BLOCK_FOCUS + heading.block().key());
    }

    /**
     * Lays the list out again after a heading folded or opened. When that folded away the row in focus, the focus
     * goes to the heading, so the keys and the description do not point at a row that is gone.
     */
    private void relayout(String headingKey) {
        layoutLines();
        if (focusedKey != null && focusedButton < 0) {
            boolean shown = false;
            for (Line line : navRows()) {
                shown |= focusedKey.equals(navKey(line));
            }
            if (!shown) {
                focusedKey = headingKey;
                this.described = null;
                layoutButtons();
            }
        }
        this.scroll = clampScroll(this.scroll);
        this.onScroll.accept(this.scroll);
    }

    private boolean searchEmpty() {
        return this.search == null || this.search.getValue().trim().isEmpty();
    }

    /**
     * Scrolls the list by so many pixels (a controller's right stick), taking the focus along to a row still in
     * view so it never sits off screen.
     */
    void scrollKeepingFocus(double pixels) {
        double before = this.scroll;
        this.scroll = clampScroll(this.scroll + pixels);
        this.onScroll.accept(this.scroll);
        if (this.scroll == before || !keyboardFocus || focusedButton >= 0) {
            return;
        }
        keysLead = true;
        List<Line> rows = navRows();
        int index = focusedRow(rows);
        if (index < 0) {
            return;
        }
        int view = this.bottom - this.top;
        Line line = rows.get(index);
        if (line.y() >= this.scroll + HEADER && line.y() + ROW <= this.scroll + view) {
            return;
        }
        // The first row wholly in view going down, the last one going up.
        Line pick = null;
        for (Line candidate : rows) {
            boolean visible = candidate.y() >= this.scroll && candidate.y() + ROW <= this.scroll + view;
            if (visible && (pick == null || pixels < 0)) {
                pick = candidate;
            }
        }
        if (pick != null) {
            focusedKey = navKey(pick);
            if (pick.option() != null) {
                this.described = pick.option();
            }
            layoutButtons();
        }
    }

    /** The screen's focus came to the list (a controller, or the arrow keys from the search field). */
    void focusGained() {
        keysInUse();
        List<Line> rows = navRows();
        if (focusedButton < 0 && focusedRow(rows) < 0 && !rows.isEmpty()) {
            focusRow(rows.get(0));
        }
    }

    /** The screen's focus left the list, for the search field. */
    void focusLost() {
        keyboardFocus = false;
        keysLead = false;
    }

    enum Press {
        APPLY, UNDO
    }

    /** A bottom button pressed from a controller's own button for it. */
    void press(Press press) {
        clickSound();
        (press == Press.APPLY ? this.actions.apply() : this.actions.undo()).run();
    }

    /** Lays the buttons out again, as their controller pictures come and go with the controller's use. */
    void refreshHints() {
        layoutButtons();
    }

    /** The list's area, where the screen's arrow navigation looks for the list. */
    ScreenRectangle listArea() {
        return new ScreenRectangle(this.listX, this.top, this.listW, Math.max(1, this.bottom - this.top));
    }

    /** What a screen reader says for the focused row: its name and value. */
    Component focusedNarration() {
        FlatButton button = focusedFooterButton();
        if (button != null) {
            return button.shownLabel();
        }
        List<Line> rows = navRows();
        int index = focusedRow(rows);
        if (index < 0) {
            return null;
        }
        Line line = rows.get(index);
        if (line.heading()) {
            return line.block().heading();
        }
        if (line.option() == null) {
            return Component.translatable(line.group().key());
        }
        return caption(line.option()).copy().append(": ").append(valueText(line.option()));
    }

    boolean searchFocused() {
        return this.search != null && this.search.isFocused();
    }

    boolean scrollBy(double mouseX, double mouseY, double amount) {
        if (mouseX < this.listX || mouseX > this.listX + this.listW || mouseY < this.top || mouseY > this.bottom) {
            return false;
        }
        this.scroll = clampScroll(this.scroll - amount * ROW * 2);
        this.onScroll.accept(this.scroll);
        return true;
    }

    private final FocusTarget focusTarget = new FocusTarget();

    /** The list as one element of the screen, for the screen's own focus navigation; see {@link FocusTarget}. */
    FocusTarget focusTarget() {
        return this.focusTarget;
    }

    /**
     * The list's rows are drawn here, not vanilla widgets, so for Minecraft's focus navigation the whole list is
     * one invisible element that keeps the focus and moves it between its rows itself. Controller mods (Controlify,
     * and those that work like it) move through a screen by asking it for the next focus in a direction, rather than
     * by pressing arrow keys: those requests land here and step through the rows as the arrow keys do. Up from the
     * first row gives the focus back to the screen, which moves it on to the search field above.
     */
    final class FocusTarget implements net.minecraft.client.gui.components.events.GuiEventListener,
        net.minecraft.client.gui.narration.NarratableEntry {

        private boolean focused;

        @Override
        public net.minecraft.client.gui.ComponentPath nextFocusPath(
            net.minecraft.client.gui.navigation.FocusNavigationEvent event) {
            if (event instanceof net.minecraft.client.gui.navigation.FocusNavigationEvent.ArrowNavigation arrow) {
                if (!this.focused) {
                    return net.minecraft.client.gui.ComponentPath.leaf(this);
                }
                return navigate(arrow.direction(), 1) ? net.minecraft.client.gui.ComponentPath.leaf(this) : null;
            }
            // Tab and a new screen's first focus: into the list, and on out of it (Tab itself switches sections).
            return this.focused ? null : net.minecraft.client.gui.ComponentPath.leaf(this);
        }

        @Override
        public void setFocused(boolean focused) {
            if (focused && !this.focused) {
                focusGained();
            } else if (!focused && this.focused) {
                focusLost();
            }
            this.focused = focused;
        }

        @Override
        public boolean isFocused() {
            return this.focused;
        }

        @Override
        public ScreenRectangle getRectangle() {
            return listArea();
        }

        @Override
        public NarrationPriority narrationPriority() {
            return this.focused ? NarrationPriority.FOCUSED : NarrationPriority.NONE;
        }

        @Override
        public void updateNarration(net.minecraft.client.gui.narration.NarrationElementOutput output) {
            Component text = focusedNarration();
            if (text != null) {
                output.add(net.minecraft.client.gui.narration.NarratedElementType.TITLE, text);
            }
        }
    }

    // ---- helpers ----

    /** The section shown; kept while the game runs, so the screen reopens on it. */
    private static RadianteOptionsScreen.Category selected = RadianteOptionsScreen.Category.QUALITY;

    private RadianteOptionsScreen.Category sectionInView() {
        return selected;
    }

    private static boolean inside(double x, double y, int rx, int ry, int rw, int rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    /**
     * Looked up once per screen: asking the resource manager for a missing file walks every pack and throws on each,
     * and doing that every frame stalled the render thread.
     */
    private static final java.util.Map<String, java.util.Optional<Identifier>> textures = new java.util.HashMap<>();

    private Identifier texture(String name) {
        return textures.computeIfAbsent(name, key -> {
            Identifier id = Identifier.fromNamespaceAndPath("radiante", "textures/gui/settings/" + key + ".png");
            return Minecraft.getInstance().getResourceManager().getResource(id).isPresent() ? java.util.Optional.of(id)
                : java.util.Optional.empty();
        }).orElse(null);
    }

    /** A toggle drawn as a checkbox: plain On/Off. One with its own words for the two states shows them instead. */
    private static boolean isCheckbox(OptionInstance<?> option) {
        if (!(option.get() instanceof Boolean)) {
            return false;
        }
        String text = valueText(option).getString();
        return text.equals(Component.translatable("options.on").getString())
            || text.equals(Component.translatable("options.off").getString());
    }

    private static OptionInstanceAccessor accessor(OptionInstance<?> option) {
        return (OptionInstanceAccessor) (Object) option;
    }

    /** The option's name; a pipeline attribute without a translation gets its own name made readable. */
    private static Component caption(OptionInstance<?> option) {
        Component caption = accessor(option).radiante$caption();
        String key = keyOf(caption);
        return key == null ? caption : AdvancedOptions.caption(key);
    }

    /** The value alone: the option's own "Name: value" text without the name in front. */
    private static Component valueText(OptionInstance<?> option) {
        Component full = accessor(option).radiante$toString().apply(option.get());
        String name = caption(option).getString();
        String text = full.getString();
        if (text.startsWith(name + ": ")) {
            return Component.literal(text.substring(name.length() + 2));
        }
        return full;
    }

    private static Tooltip tooltipOf(OptionInstance<?> option) {
        OptionInstance.TooltipSupplier<Object> supplier = accessor(option).radiante$tooltip();
        return supplier == null ? null : supplier.apply(option.get());
    }

    /**
     * What the focus remembers an option by: its caption's translation key, taken before an untranslated pipeline
     * attribute's key is turned into a readable name (which has no key left), or else the name itself.
     */
    private static String focusKey(OptionInstance<?> option) {
        String key = keyOf(accessor(option).radiante$caption());
        return key != null ? key : caption(option).getString();
    }

    private static String keyOf(Component caption) {
        return caption.getContents() instanceof TranslatableContents translatable ? translatable.getKey() : null;
    }
}
