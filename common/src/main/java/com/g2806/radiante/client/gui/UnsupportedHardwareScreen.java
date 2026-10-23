package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.render.RadianteRenderer;
import net.minecraft.client.PreferredGraphicsApi;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.MultiLineTextWidget;
import net.minecraft.client.gui.layouts.LinearLayout;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

/**
 * Shown once when the ray tracer cannot start on this machine. Saying nothing would leave the player with a game
 * that looks like vanilla and no idea why, so this explains it, by the cause that was actually met: the game put on
 * OpenGL by the player, OpenGL forced for one session after a start that never finished (often a second instance
 * opened at the same time), a graphics card that cannot ray trace, or a renderer that failed to start on one that
 * can. Each was once told as an unsupported graphics card.
 */
public class UnsupportedHardwareScreen extends Screen {

    private static final int WIDGET_WIDTH = 240;

    private static boolean shown;

    private enum Reason {
        OPENGL("opengl"),
        FAILED_START("failed_start"),
        NO_RAY_TRACING(""),
        INIT_FAILED("init_failed");

        private final String suffix;

        Reason(String suffix) {
            this.suffix = suffix;
        }

        String key(String base) {
            return this.suffix.isEmpty() ? base : base + "." + this.suffix;
        }
    }

    private final Screen parent;
    private final Reason reason;

    public UnsupportedHardwareScreen(Screen parent) {
        this(parent, reason());
    }

    private UnsupportedHardwareScreen(Screen parent, Reason reason) {
        super(Component.translatable(reason.key("screen.radiante.unsupported.title")));
        this.parent = parent;
        this.reason = reason;
    }

    private static Reason reason() {
        return switch (RadianteRenderer.failure()) {
            case NO_RAY_TRACING -> Reason.NO_RAY_TRACING;
            case INIT_FAILED -> Reason.INIT_FAILED;
            case NOT_VULKAN -> choseOpenGl() || !Options.openGlAfterFailedStart ? Reason.OPENGL : Reason.FAILED_START;
        };
    }

    /** True when the player put the game on OpenGL, a setting rather than a limit of the machine. */
    private static boolean choseOpenGl() {
        if (Options.useOpenGl) {
            return true;
        }
        net.minecraft.client.Minecraft minecraft = net.minecraft.client.Minecraft.getInstance();
        return minecraft != null && minecraft.options != null
            && minecraft.options.preferredGraphicsBackend().get() == PreferredGraphicsApi.OPENGL;
    }

    /** True once, so the warning appears on the first menu after startup and never nags again. */
    public static boolean shouldShow() {
        if (shown) {
            return false;
        }
        shown = true;
        return true;
    }

    @Override
    protected void init() {
        LinearLayout layout = LinearLayout.vertical().spacing(8);
        layout.defaultCellSetting().alignHorizontallyCenter();

        layout.addChild(new MultiLineTextWidget(this.title, this.font).setMaxWidth(320).setCentered(true));
        layout.addChild(new MultiLineTextWidget(
            Component.translatable(this.reason.key("screen.radiante.unsupported.message")), this.font)
            .setMaxWidth(320).setCentered(true));

        switch (this.reason) {
            case OPENGL -> {
                layout.addChild(Button.builder(Component.translatable("screen.radiante.unsupported.vulkan"), button -> {
                    this.minecraft.options.preferredGraphicsBackend().set(PreferredGraphicsApi.VULKAN);
                    Options.useOpenGl = false;
                    Options.overwriteConfig();
                    this.minecraft.options.save();
                    this.minecraft.gui.setScreen(new OpenGlSwitchedScreen(this.parent, true));
                }).width(WIDGET_WIDTH).build());
                layout.addChild(continueButton());
            }
            case NO_RAY_TRACING -> {
                layout.addChild(continueButton());
                layout.addChild(Button.builder(Component.translatable("screen.radiante.unsupported.opengl"), button -> {
                    Options.useOpenGl = true;
                    Options.overwriteConfig();
                    this.minecraft.gui.setScreen(new OpenGlSwitchedScreen(this.parent, false));
                }).width(WIDGET_WIDTH).build());
            }
            // Nothing to switch: a restart is all it takes.
            case FAILED_START, INIT_FAILED -> layout.addChild(continueButton());
        }

        layout.arrangeElements();
        layout.visitWidgets(this::addRenderableWidget);
        layout.setPosition(this.width / 2 - layout.getWidth() / 2, this.height / 2 - layout.getHeight() / 2);
    }

    private Button continueButton() {
        return Button.builder(Component.translatable("screen.radiante.unsupported.continue"),
            button -> this.onClose()).width(WIDGET_WIDTH).build();
    }

    @Override
    public void onClose() {
        this.minecraft.gui.setScreen(this.parent);
    }
}
