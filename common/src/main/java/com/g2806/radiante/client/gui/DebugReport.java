package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.render.RadianteRenderer;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import net.minecraft.SharedConstants;
import net.minecraft.client.Minecraft;

/**
 * What a bug report needs in one block of text: versions, GPU, the running pipeline and every pipeline setting, read
 * from what is applied rather than what is staged on the settings screen.
 */
final class DebugReport {

    private DebugReport() {
    }

    static String build(Minecraft minecraft) {
        StringBuilder text = new StringBuilder("Radiante ").append(com.g2806.radiante.platform.RadiantePlatform.INSTANCE.modVersion())
            .append(" on Minecraft ").append(SharedConstants.getCurrentVersion().name()).append('\n');
        text.append("OS: ").append(System.getProperty("os.name")).append(' ').append(System.getProperty("os.version"))
            .append(", Java ").append(System.getProperty("java.version")).append('\n');
        text.append("GPU: ").append(RadianteRenderer.gpuIdentity()).append('\n');
        text.append(String.format(Locale.ROOT, "Window: %dx%d, render distance %d\n",
            minecraft.getWindow().getWidth(), minecraft.getWindow().getHeight(),
            minecraft.options.getEffectiveRenderDistance()));
        text.append("Ray tracing: ").append(Options.rayTracingEnabled ? "on" : "off").append('\n');
        text.append("Pipeline: ").append(Pipeline.INSTANCE.getActivePresetName()).append(", upscaler ")
            .append(Pipeline.describeUpscaler()).append('\n');
        text.append("Shader pack: ").append(Pipeline.shaderPackPath()).append('\n');
        text.append("Debug view: ").append(Pipeline.debugView()).append('\n');
        text.append("HDR: ").append(com.g2806.radiante.client.hdr.HdrDisplay.isActive() ? "on" : "off").append('\n');
        try {
            String stats = Pipeline.gpuStats();
            if (stats != null && !stats.isEmpty()) {
                text.append("GPU time: ").append(stats).append('\n');
            }
        } catch (LinkageError notLoaded) {
            // The renderer did not load on this machine; the rest of the report says why it matters.
            text.append("GPU time: renderer not loaded\n");
        }
        for (String module : AdvancedOptions.MODULES) {
            Map<String, String> values = new TreeMap<>(Pipeline.moduleValues(module));
            if (values.isEmpty()) {
                continue;
            }
            text.append('\n').append(module).append('\n');
            String prefix = module.substring(0, module.length() - "name".length()) + "attribute.";
            values.forEach((name, value) -> text.append("  ")
                .append(name.startsWith(prefix) ? name.substring(prefix.length()) : name).append(" = ")
                .append(value).append('\n'));
        }
        return text.toString();
    }

    /** To the clipboard and to the game log, so it is there too if the game is gone by the time it is asked for. */
    static void copy(Minecraft minecraft) {
        String report = build(minecraft);
        minecraft.keyboardHandler.setClipboard(report);
        RadianteClient.LOGGER.info("[debug report]\n{}", report);
    }
}
