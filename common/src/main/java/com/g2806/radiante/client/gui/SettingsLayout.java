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
import net.minecraft.network.chat.Component;
import net.minecraft.network.chat.contents.TranslatableContents;
import net.minecraft.resources.Identifier;

/**
 * The Radiante settings screen, after Sodium's own: a search field along the top; on the left the mod's name and one
 * entry per section, the one in view marked with a bar; on the right every section in one scrolling list, each under
 * its own heading, options as flat rows with the name on the left and the value (or a checkbox) on the right. Beside
 * the list, the description and cost of the option under the mouse. Actions (Reset, Advanced, Undo, Apply, Done) are
 * flat text buttons along the bottom.
 *
 * <p>Rows are handled here rather than as vanilla widgets; {@link SettingsLayoutDraw} draws them and
 * {@link SettingsLayoutNav} moves keyboard/controller focus through them.
 */
final class SettingsLayout {

    /**
     * A section and its options, in order; then its blocks, each under a heading of its own (the addons). {@code title}
     * and {@code icon} name a section that is not one of Radiante's own (a shader pack's page, named after the pack);
     * null for the category's name and picture.
     */
    record Section(RadianteOptionsScreen.Category category, List<OptionInstance<?>> options, List<Group> groups,
                   List<Block> blocks, Component title, AddonIcons.Icon icon) {
        Section(RadianteOptionsScreen.Category category, List<OptionInstance<?>> options, List<Group> groups,
                List<Block> blocks) {
            this(category, options, groups, blocks, null, null);
        }

        Section(RadianteOptionsScreen.Category category, List<OptionInstance<?>> options, List<Group> groups) {
            this(category, options, groups, List.of());
        }

        Component name() {
            return this.title != null ? this.title : Component.translatable(this.category.key());
        }
    }

    /**
     * Options under a heading that folds them away, as each addon's are in the Addons section; {@code key} (the
     * addon's id) is what its open or folded state is remembered by. {@code empty} is shown under the heading when
     * there are no options; a search that finds the heading shows all of them. {@code icon} (the addon's, null for
     * none) is drawn left of the heading. {@code advanced} are shown under the others, a step in and dimmer, only while
     * Advanced is on (a shader pack's advanced settings in their group); {@code openByDefault} keeps a block open until
     * the player folds it, where several blocks would otherwise start folded.
     */
    record Block(String key, Component heading, AddonIcons.Icon icon, List<OptionInstance<?>> options,
                 Component empty, List<OptionInstance<?>> advanced, boolean openByDefault) {
        Block(String key, Component heading, AddonIcons.Icon icon, List<OptionInstance<?>> options, Component empty) {
            this(key, heading, icon, options, empty, List.of(), false);
        }
    }

    /**
     * Advanced options about one thing, under a heading that folds them away. {@code title} names a group whose key
     * is not a translation key (a shader pack's); null for the key's translation.
     */
    record Group(String key, List<OptionInstance<?>> options, Component title) {
        Group(String key, List<OptionInstance<?>> options) {
            this(key, options, null);
        }

        Component name() {
            return this.title != null ? this.title : Component.translatable(this.key);
        }
    }

    /**
     * What the buttons do. {@code advanced} is whether the Advanced options show; the toggle flips it.
     * {@code shaderPacks} opens the shader pack list (null for no button).
     */
    record Actions(Runnable reset, Runnable optimizeGpu, Runnable undo, Runnable apply, Runnable done, Extra extra,
                   Runnable share, boolean advanced, Runnable toggleAdvanced, Runnable rebuild, Runnable shaderPacks,
                   Runnable photoMode) {
    }

    /**
     * A button that belongs to one option, beside Advanced: installing or deleting an upscaler. It shows once the
     * option with the caption {@code anchorKey} was clicked, or always when that is null. Null for no button.
     */
    record Extra(Component label, Component tooltip, Runnable action, boolean enabled, String anchorKey) {
    }

    static String focusedKey;
    static int focusedButton = -1;
    static final java.util.Set<String> openGroups = new java.util.HashSet<>();
    static final String GROUP_FOCUS = "#group:";
    static final String BLOCK_FOCUS = "#block:";

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

    static boolean blockOpen(Section section, Block block) {
        Boolean state = blockStates().get(block.key());
        return state != null ? state : block.openByDefault() || section.blocks().size() == 1;
    }

    static void setBlockOpen(String key, boolean open) {
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

    static void clearFocus() {
        if (Minecraft.getInstance().gui.screen() instanceof RadianteOptionsScreen) {
            return;
        }
        focusedKey = null;
        focusedButton = -1;
    }

    static final int ACCENT = Theme.ACCENT;
    static final int TEXT = Theme.TEXT;
    static final int TEXT_DIM = Theme.TEXT_DIM;
    static final int PANEL = Theme.PANEL;
    static final int PANEL_DARK = Theme.PANEL_DARK;
    static final int ROW_HOVER = Theme.ROW_HOVER;
    static final int GROUP = 0x60000000;
    /** An option that does nothing as things are set. */
    static final int TEXT_BLOCKED = 0xFF6A716E;
    static final int NOTICE = 0xFFF0B040;

    static final int ROW = Theme.ROW;
    static final int HEADER = Theme.HEADER;
    static final int GAP = Theme.GAP;
    static final int PAD = Theme.PAD;
    /** The side of the square an addon's icon is drawn in, beside its heading. */
    static final int ICON = 16;

    final Font font;
    final int width;
    final int height;
    final List<Section> sections;
    final Actions actions;
    final Consumer<Double> onScroll;
    EditBox search;

    // Laid out in init.
    int sidebarX;
    int sidebarW;
    int listX;
    int listW;
    int top;
    int bottom;
    int panelX;
    int panelW;
    double scroll;
    int contentHeight;

    record Line(Section section, OptionInstance<?> option, Group group, Component note, boolean advanced,
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

    final List<Line> lines = new ArrayList<>();
    private final List<int[]> sectionSpans = new ArrayList<>();
    OptionInstance<?> described;
    private OptionInstance<Integer> dragging;
    private int dragX;
    private int dragWidth;

    record FlatButton(Component label, Runnable action, boolean enabled, Component tooltip, int x, int y,
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

    final List<FlatButton> buttons = new ArrayList<>();

    interface RestartInfo {
        boolean needsRestart(OptionInstance<?> option);

        boolean pending(OptionInstance<?> option);
    }

    RestartInfo restart = new RestartInfo() {
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

    interface Availability {
        Component blocked(String optionKey);

        List<Component> affects(String optionKey);

        default Component notice(String optionKey) {
            return null;
        }

        default OptionImpact impact(OptionInstance<?> option) {
            return null;
        }
    }

    Availability availability = new Availability() {
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

    boolean blocked(OptionInstance<?> option) {
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
                this.width - margin - topRight - 4 - packsW, 13, packsW));
            topRight += packsW + 4;
        }
        if (this.actions.photoMode() != null) {
            FlatButton photo = flat("options.radiante.photo_mode", "options.radiante.photo_mode.tooltip",
                this.actions.photoMode(), true);
            int photoW = this.font.width(photo.label()) + 16;
            this.buttons.add(new FlatButton(photo.label(), photo.action(), true, photo.tooltip(),
                this.width - margin - topRight - 4 - photoW, 13, photoW));
            topRight += photoW + 4;
        }
        if (this.actions.optimizeGpu() != null) {
            int optW = this.sidebarW - 8;
            Component optLabel = Component.translatable("options.radiante.optimize_gpu");
            if (this.font.width(optLabel) > optW - 8) {
                optLabel = Component.literal(this.font.plainSubstrByWidth(optLabel.getString(),
                    Math.max(0, optW - 8 - this.font.width("…"))) + "…");
            }
            this.buttons.add(new FlatButton(optLabel, this.actions.optimizeGpu(), true,
                Component.translatable("options.radiante.optimize_gpu.tooltip"),
                this.sidebarX + 4, this.bottom - 20, optW));
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
    int topButtons;

    /** The buttons along the bottom; laid out again when the option in focus changes, as one of them follows it. */
    void layoutButtons() {
        while (this.buttons.size() > this.topButtons) {
            this.buttons.remove(this.buttons.size() - 1);
        }
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
    void layoutLines() {
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
            List<Group> groups = new ArrayList<>();
            int hidden = 0;
            for (Group group : section.groups()) {
                List<OptionInstance<?>> matches = matching(group.options(), filter);
                if (matches.isEmpty()) {
                    continue;
                }
                if (this.actions.advanced()) {
                    groups.add(new Group(group.key(), matches, group.title()));
                } else if (!filter.isEmpty()) {
                    hidden += matches.size();
                }
            }
            List<Block> blocks = new ArrayList<>();
            for (Block block : section.blocks()) {
                boolean named = !filter.isEmpty()
                    && block.heading().getString().toLowerCase(Locale.ROOT).contains(filter);
                List<OptionInstance<?>> matches = named ? block.options() : matching(block.options(), filter);
                List<OptionInstance<?>> advancedMatches = named ? block.advanced() : matching(block.advanced(), filter);
                if (!this.actions.advanced()) {
                    if (!filter.isEmpty()) {
                        hidden += advancedMatches.size();
                    }
                    advancedMatches = List.of();
                    if (block.options().isEmpty() && !block.advanced().isEmpty()) {
                        continue;
                    }
                }
                if (filter.isEmpty() || named || !matches.isEmpty() || !advancedMatches.isEmpty()) {
                    blocks.add(new Block(block.key(), block.heading(), block.icon(), matches, block.empty(),
                        advancedMatches, block.openByDefault()));
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
                if (filter.isEmpty() && !blockOpen(section, block)) {
                    continue;
                }
                for (OptionInstance<?> option : block.options()) {
                    this.lines.add(new Line(section, option, null, null, false, y, ROW));
                    y += ROW;
                }
                for (OptionInstance<?> option : block.advanced()) {
                    this.lines.add(new Line(section, option, null, null, true, y, ROW));
                    y += ROW;
                }
                if (block.options().isEmpty() && block.advanced().isEmpty() && block.empty() != null) {
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

    double clampScroll(double value) {
        return Math.max(0.0, Math.min(value, Math.max(0, this.contentHeight - (this.bottom - this.top))));
    }

    void extract(GuiGraphicsExtractor g, int mouseX, int mouseY) {
        SettingsLayoutDraw.extract(this, g, mouseX, mouseY);
    }

    // ---- input ----

    static void clickSound() {
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
                focusedKey = focusKey(line.option());
                layoutButtons();
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
        if (value instanceof com.g2806.radiante.client.option.EmissionColor) {
            BlockEmissionSettings.open(option);
        } else if (value instanceof Boolean on) {
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

    static boolean keyboardFocus;
    static boolean keysLead;
    int lastMouseX = Integer.MIN_VALUE;
    int lastMouseY;

    static void keysInUse() {
        keyboardFocus = true;
        keysLead = true;
    }

    boolean keyPressed(KeyEvent event) {
        return SettingsLayoutNav.keyPressed(this, event);
    }

    void section(int direction) {
        SettingsLayoutNav.section(this, direction);
    }

    /** The rows the keys stop on: options, and the headings of the advanced groups and of the addons. */
    List<Line> navRows() {
        List<Line> rows = new ArrayList<>();
        for (Line line : this.lines) {
            if (line.option() != null || line.group() != null || line.heading()) {
                rows.add(line);
            }
        }
        return rows;
    }

    static String navKey(Line line) {
        return line.option() != null ? focusKey(line.option())
            : line.heading() ? BLOCK_FOCUS + line.block().key() : GROUP_FOCUS + line.group().key();
    }

    /** The row in focus, or else the one under the mouse; -1 for none. */
    int focusedRow(List<Line> rows) {
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

    List<FlatButton> footer() {
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

    FlatButton focusedFooterButton() {
        if (!keyboardFocus || focusedButton < 0) {
            return null;
        }
        List<FlatButton> footer = footer();
        return footer.isEmpty() ? null : footer.get(Math.min(focusedButton, footer.size() - 1));
    }

    void focusRow(Line line) {
        focusedKey = navKey(line);
        focusedButton = -1;
        if (line.option() != null) {
            this.described = line.option();
        }
        keysInUse();
        int view = this.bottom - this.top;
        if (line.y() < this.scroll + HEADER) {
            this.scroll = clampScroll(line.y() - HEADER);
        } else if (line.y() + ROW > this.scroll + view) {
            this.scroll = clampScroll(line.y() + ROW - view);
        }
        this.onScroll.accept(this.scroll);
        layoutButtons();
    }

    boolean navigate(ScreenDirection direction, int amount) {
        return SettingsLayoutNav.navigate(this, direction, amount);
    }

    void toggleGroup(Group group) {
        clickSound();
        if (!openGroups.remove(group.key())) {
            openGroups.add(group.key());
        }
        relayout(GROUP_FOCUS + group.key());
    }

    void toggleBlock(Line heading) {
        if (!searchEmpty()) {
            return;
        }
        clickSound();
        setBlockOpen(heading.block().key(), !blockOpen(heading.section(), heading.block()));
        relayout(BLOCK_FOCUS + heading.block().key());
    }

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

    boolean searchEmpty() {
        return this.search == null || this.search.getValue().trim().isEmpty();
    }

    void scrollKeepingFocus(double pixels) {
        SettingsLayoutNav.scrollKeepingFocus(this, pixels);
    }

    void focusGained() {
        SettingsLayoutNav.focusGained(this);
    }

    void focusLost() {
        SettingsLayoutNav.focusLost();
    }

    enum Press {
        APPLY, UNDO
    }

    void press(Press press) {
        clickSound();
        (press == Press.APPLY ? this.actions.apply() : this.actions.undo()).run();
    }

    void refreshHints() {
        layoutButtons();
    }

    ScreenRectangle listArea() {
        return new ScreenRectangle(this.listX, this.top, this.listW, Math.max(1, this.bottom - this.top));
    }

    Component focusedNarration() {
        return SettingsLayoutNav.focusedNarration(this);
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

    FocusTarget focusTarget() {
        return this.focusTarget;
    }

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

    static RadianteOptionsScreen.Category selected = RadianteOptionsScreen.Category.QUALITY;

    static void select(RadianteOptionsScreen.Category category) {
        selected = category;
    }

    RadianteOptionsScreen.Category sectionInView() {
        return selected;
    }

    static boolean inside(double x, double y, int rx, int ry, int rw, int rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    private static final java.util.Map<String, java.util.Optional<Identifier>> textures = new java.util.HashMap<>();

    Identifier texture(String name) {
        return textures.computeIfAbsent(name, key -> {
            Identifier id = Identifier.fromNamespaceAndPath("radiante", "textures/gui/settings/" + key + ".png");
            return Minecraft.getInstance().getResourceManager().getResource(id).isPresent() ? java.util.Optional.of(id)
                : java.util.Optional.empty();
        }).orElse(null);
    }

    static boolean isCheckbox(OptionInstance<?> option) {
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

    static Component caption(OptionInstance<?> option) {
        Component caption = accessor(option).radiante$caption();
        String key = keyOf(caption);
        return key == null || key.startsWith(BlockEmissionSettings.PREFIX) ? caption : AdvancedOptions.caption(key);
    }

    static Component valueText(OptionInstance<?> option) {
        Component full = accessor(option).radiante$toString().apply(option.get());
        String name = caption(option).getString();
        String text = full.getString();
        if (text.startsWith(name + ": ")) {
            return Component.literal(text.substring(name.length() + 2));
        }
        return full;
    }

    static Tooltip tooltipOf(OptionInstance<?> option) {
        OptionInstance.TooltipSupplier<Object> supplier = accessor(option).radiante$tooltip();
        return supplier == null ? null : supplier.apply(option.get());
    }

    static String focusKey(OptionInstance<?> option) {
        String key = keyOf(accessor(option).radiante$caption());
        return key != null ? key : caption(option).getString();
    }

    static String keyOf(Component caption) {
        return caption.getContents() instanceof TranslatableContents translatable ? translatable.getKey() : null;
    }
}
