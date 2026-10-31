package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.option.EmissionColor;
import java.util.Locale;
import java.util.function.Consumer;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.AbstractSliderButton;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

/** An HDR tint is a linear RGB colour and exposure; the numeric fields also accept channels above one directly. */
final class HdrEmissionScreen extends Screen {
    private static final String KEY = "options.radiante.emission_colour.";
    private final Screen parent;
    private final Consumer<EmissionColor> save;
    private EmissionColor colour;
    private final double[] channels = new double[3];
    private double exposure;
    private final EditBox[] fields = new EditBox[3];
    private final ChannelSlider[] sliders = new ChannelSlider[4];
    private Button done;
    private boolean updating;

    HdrEmissionScreen(Screen parent, Component title, EmissionColor colour, Consumer<EmissionColor> save) {
        super(title);
        this.parent = parent;
        this.save = save;
        this.colour = colour;
        decompose();
    }

    private void decompose() {
        double gain = Math.max(1, colour.peak());
        exposure = Math.log(gain) / Math.log(2);
        channels[0] = colour.red() / gain;
        channels[1] = colour.green() / gain;
        channels[2] = colour.blue() / gain;
    }

    @Override
    protected void init() {
        int left = width / 2 - 150;
        int top = Math.max(45, height / 2 - 78);
        for (int i = 0; i < 3; i++) {
            final int channel = i;
            fields[i] = new EditBox(font, left + 220, top + i * 28, 80, 20, label(i));
            fields[i].setMaxLength(14);
            fields[i].setValue(Float.toString(component(i)));
            fields[i].setResponder(text -> readFields());
            addRenderableWidget(fields[i]);
            sliders[i] = addRenderableWidget(new ChannelSlider(left, top + i * 28, 210, i));
        }
        sliders[3] = addRenderableWidget(new ChannelSlider(left, top + 88, 300, 3));
        addRenderableWidget(Button.builder(Component.translatable("controls.reset"), button -> {
            colour = EmissionColor.WHITE; decompose(); refresh();
        }).bounds(left, top + 120, 94, 20).build());
        addRenderableWidget(Button.builder(Component.translatable("gui.cancel"), button -> onClose())
            .bounds(left + 103, top + 120, 94, 20).build());
        done = addRenderableWidget(Button.builder(Component.translatable("gui.done"), button -> {
            save.accept(colour); onClose();
        }).bounds(left + 206, top + 120, 94, 20).build());
    }

    private static Component label(int channel) {
        return Component.translatable(KEY + new String[] {"red", "green", "blue", "exposure"}[channel]);
    }

    private float component(int channel) {
        return switch (channel) { case 0 -> colour.red(); case 1 -> colour.green(); default -> colour.blue(); };
    }

    private void readFields() {
        if (updating || done == null) return;
        try {
            float[] rgb = new float[3];
            for (int i = 0; i < 3; i++) {
                rgb[i] = Float.parseFloat(fields[i].getValue());
                if (!Float.isFinite(rgb[i]) || rgb[i] < 0 || rgb[i] > EmissionColor.MAX) throw new NumberFormatException();
            }
            colour = new EmissionColor(rgb[0], rgb[1], rgb[2]);
            decompose();
            for (ChannelSlider slider : sliders) slider.sync();
            done.active = true;
        } catch (NumberFormatException invalid) {
            done.active = false;
        }
    }

    private void refresh() {
        updating = true;
        for (int i = 0; i < 3; i++) fields[i].setValue(String.format(Locale.ROOT, "%.6g", component(i)));
        for (ChannelSlider slider : sliders) slider.sync();
        if (done != null) done.active = true;
        updating = false;
    }

    private final class ChannelSlider extends AbstractSliderButton {
        private final int channel;
        ChannelSlider(int x, int y, int width, int channel) {
            super(x, y, width, 20, Component.empty(), channel == 3 ? (exposure + 12) / 28 : channels[channel]);
            this.channel = channel;
            updateMessage();
        }
        void sync() { value = channel == 3 ? (exposure + 12) / 28 : channels[channel]; updateMessage(); }
        @Override protected void updateMessage() {
            String text = channel == 3 ? String.format(Locale.ROOT, "%+.2f EV", exposure)
                : String.format(Locale.ROOT, "%.3f", channels[channel]);
            setMessage(Component.empty().append(label(channel)).append(": " + text));
        }
        @Override protected void applyValue() {
            if (channel == 3) exposure = value * 28 - 12;
            else channels[channel] = value;
            double gain = Math.pow(2, exposure);
            colour = new EmissionColor((float) (channels[0] * gain), (float) (channels[1] * gain), (float) (channels[2] * gain));
            refresh();
        }
    }

    @Override
    public void extractRenderState(GuiGraphicsExtractor g, int mouseX, int mouseY, float partialTick) {
        super.extractRenderState(g, mouseX, mouseY, partialTick);
        int top = Math.max(45, height / 2 - 78);
        g.centeredText(font, title, width / 2, top - 34, 0xFFFFFFFF);
        g.centeredText(font, Component.translatable(KEY + "hint"), width / 2, top - 20, 0xFFB0B8B4);
        g.fill(width / 2 + 158, top, width / 2 + 178, top + 76, colour.preview());
    }

    @Override public void onClose() { minecraft.gui.setScreen(parent); }
}
