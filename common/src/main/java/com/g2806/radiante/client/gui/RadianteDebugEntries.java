package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.render.FrameGeneration;
import com.g2806.radiante.client.render.RadianteRenderer;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import net.minecraft.client.Minecraft;
import net.minecraft.client.core.registries.ClientRegistries;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.components.debug.DebugGroups;
import net.minecraft.client.gui.components.debug.DebugScreenDisplayer;
import net.minecraft.client.gui.components.debug.DebugScreenEntry;
import net.minecraft.client.gui.components.debug.DebugScreenEntryStatus;
import net.minecraft.core.Registry;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.chunk.LevelChunk;
import org.jspecify.annotations.Nullable;

/**
 * Radiante's lines on the F3 screen, each its own debug entry so it can be hidden from the debug options screen
 * (F3 + F6) like any of Minecraft's.
 */
public final class RadianteDebugEntries {

    public static final ResourceKey<DebugScreenEntry> UPSCALER = key("upscaler");
    public static final ResourceKey<DebugScreenEntry> REFLEX = key("reflex");
    public static final ResourceKey<DebugScreenEntry> FRAME_GENERATION = key("frame_generation");
    public static final ResourceKey<DebugScreenEntry> HDR = key("hdr");
    public static final ResourceKey<DebugScreenEntry> GPU_TIME = key("gpu_time");
    public static final ResourceKey<DebugScreenEntry> SCENE = key("scene");
    /** Shown in the overlay until the player hides them. */
    public static final List<ResourceKey<DebugScreenEntry>> ALL = List.of(UPSCALER, GPU_TIME, SCENE, REFLEX, FRAME_GENERATION, HDR);

    private RadianteDebugEntries() {
    }

    private static ResourceKey<DebugScreenEntry> key(String name) {
        return ResourceKey.create(ClientRegistries.DEBUG_SCREEN_ENTRY, Identifier.fromNamespaceAndPath("radiante", name));
    }

    /** Whether the F3 screen lists the GPU's pass timings, the same choice as in the debug options screen (F3 + F6). */
    static boolean gpuTimingsShown() {
        return Minecraft.getInstance().debugEntries.getStatus(GPU_TIME) != DebugScreenEntryStatus.NEVER;
    }

    /** The GPU's timings and the scene's counts go together: the renderer's numbers on F3. */
    static void setGpuTimingsShown(boolean shown) {
        DebugScreenEntryStatus status = shown ? DebugScreenEntryStatus.IN_OVERLAY : DebugScreenEntryStatus.NEVER;
        Minecraft.getInstance().debugEntries.setStatus(GPU_TIME, status);
        Minecraft.getInstance().debugEntries.setStatus(SCENE, status);
    }

    /** Called while Minecraft fills the debug entry registry (DebugScreenEntriesMixin). */
    public static void register(Registry<DebugScreenEntry> registry) {
        Registry.register(registry, UPSCALER, new Entry() {
            @Override
            void lines(DebugScreenDisplayer displayer) {
                displayer.addToGroup(DebugGroups.MISC, "Upscaler: " + Pipeline.describeUpscaler());
            }
        });
        Registry.register(registry, GPU_TIME, new Entry() {
            @Override
            void lines(DebugScreenDisplayer displayer) {
                // Measured on the GPU with timestamps: what the frame costs there, whatever the CPU is doing.
                String stats = Pipeline.gpuStats();
                if (stats != null && !stats.isEmpty()) {
                    displayer.addToGroup(DebugGroups.MISC, wrapGpuStats("PrismRT GPU: " + stats));
                }
            }
        });
        Registry.register(registry, SCENE, new Entry() {
            @Override
            void lines(DebugScreenDisplayer displayer) {
                long[] stats = Pipeline.sceneStats();
                displayer.addToGroup(DebugGroups.MISC, String.format(Locale.ROOT, "Scene: %d TLAS instances, %d entity BLAS/frame, %d sections pending",
                    stats[0], stats[1], com.g2806.radiante.client.render.ChunkManager.pendingBuilds()));
                displayer.addToGroup(DebugGroups.MISC, String.format(Locale.ROOT, "Renderer VRAM: %d / %d MB", stats[2], stats[3]));
            }
        });
        Registry.register(registry, REFLEX, new Entry() {
            @Override
            void lines(DebugScreenDisplayer displayer) {
                String reflex;
                if (!com.g2806.radiante.client.render.FrameGeneration.isReflexSupported()) {
                    reflex = "unavailable";
                } else {
                    reflex = Options.reflex ? "On" : "Off";
                }
                displayer.addToGroup(DebugGroups.MISC, "NVIDIA Reflex: " + reflex);
            }
        });
        Registry.register(registry, HDR, new Entry() {
            @Override
            void lines(DebugScreenDisplayer displayer) {
                if (!Options.hdrOutput) {
                    return;
                }
                displayer.addToGroup(DebugGroups.MISC, com.g2806.radiante.client.hdr.HdrDisplay.isActive()
                    ? String.format(Locale.ROOT, "HDR: on (scRGB), peak %d nits, paper white %d nits%s",
                        Options.hdrPeakNits, Options.hdrPaperWhiteNits, Options.hdrDebugView ? ", debug view" : "")
                    : "HDR: not running (restart, and check HDR is on in Windows)");
            }
        });
        Registry.register(registry, FRAME_GENERATION, new Entry() {
            @Override
            void lines(DebugScreenDisplayer displayer) {
                // Minecraft's own counter only sees the frames it rendered; with frame generation on the screen
                // refreshes several times faster.
                int presented = FrameGeneration.presentedFrameRate();
                if (presented <= 0) {
                    return;
                }
                int rendered = Minecraft.getInstance().getFps();
                int generated = Math.max(0, presented - rendered);
                displayer.addToGroup(DebugGroups.MISC, String.format(Locale.ROOT, "%d fps on screen (%d generated, %dx)", presented,
                    generated, FrameGeneration.multiplier()));
            }
        });
    }

    /**
     * "PrismRT GPU: 9.12 ms (world 5.10, DLSS 1.20, ...)" split at the commas into lines that fit half the screen, so
     * it does not run into the right-hand column of the debug screen.
     */
    static List<String> wrapGpuStats(String text) {
        Minecraft minecraft = Minecraft.getInstance();
        // The debug screen draws each column with a 2 px margin and a 1 px background padding on each side.
        int maxWidth = minecraft.getWindow().getGuiScaledWidth() / 2 - 8;
        Font font = minecraft.font;
        if (font.width(text) <= maxWidth) {
            return List.of(text);
        }
        List<String> lines = new ArrayList<>();
        StringBuilder line = new StringBuilder();
        for (String part : text.split("(?<=, )|(?<=ms )")) {
            if (line.length() > 0 && font.width(line + part.stripTrailing()) > maxWidth) {
                lines.add(line.toString().stripTrailing());
                line.setLength(0);
                line.append("  ");
            }
            line.append(part);
        }
        if (line.length() > 0) {
            lines.add(line.toString().stripTrailing());
        }
        return lines;
    }

    private abstract static class Entry implements DebugScreenEntry {

        abstract void lines(DebugScreenDisplayer displayer);

        @Override
        public void display(DebugScreenDisplayer displayer, @Nullable Level serverOrClientLevel,
            @Nullable LevelChunk clientChunk, @Nullable LevelChunk serverChunk) {
            if (RadianteRenderer.isRayTracingEnabled()) {
                this.lines(displayer);
            }
        }
    }
}
