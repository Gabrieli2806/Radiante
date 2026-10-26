package com.g2806.radiante.client;

import com.mojang.blaze3d.pipeline.RenderTarget;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.renderpearl.api.buffers.GpuBuffer;
import com.mojang.renderpearl.api.buffers.GpuBufferSlice;
import com.mojang.renderpearl.api.textures.GpuTexture;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import net.minecraft.client.Minecraft;

/**
 * Development only: records every frame the game draws, GUI included, into a video through ffmpeg, for trailers and
 * side-by-side comparisons. Each frame is copied out of the main render target once it is finished, as a screenshot
 * is, and piped to ffmpeg as raw RGBA, so the video has exactly the frames drawn, with no frame dropped or doubled
 * the way a desktop capture would. ffmpeg is {@code RADIANTE_DEV_FFMPEG} (or {@code ffmpeg} on the PATH) and the
 * videos go to {@code RADIANTE_DEV_CAPTURE_DIR} (or {@code captures/} in the game folder).
 */
public final class DevRecorder {

    private static final byte[] END = new byte[0];

    private static Process ffmpeg;
    private static Thread writer;
    private static BlockingQueue<byte[]> queue;
    private static final ArrayDeque<GpuBuffer> FREE_BUFFERS = new ArrayDeque<>();
    private static final ArrayDeque<byte[]> FREE_FRAMES = new ArrayDeque<>();
    private static int width;
    private static int height;
    private static long frames;
    private static boolean recording;
    private static Path output;
    private static long delivered;
    private static boolean stopping;

    private static void finishIfDone() {
        if (stopping && delivered >= frames) {
            stopping = false;
            try {
                queue.put(END);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    private DevRecorder() {
    }

    static boolean isRecording() {
        return recording;
    }

    static void start(Minecraft minecraft, String name, int fps) {
        if (recording || stopping) {
            RadianteClient.LOGGER.warn("[dev] recorder busy; {} not recorded", name);
            return;
        }
        RenderTarget target = minecraft.gameRenderer.mainRenderTarget();
        width = target.width;
        height = target.height;
        String exe = System.getenv().getOrDefault("RADIANTE_DEV_FFMPEG", "ffmpeg");
        String dir = System.getenv("RADIANTE_DEV_CAPTURE_DIR");
        Path folder = dir != null ? Path.of(dir) : minecraft.gameDirectory.toPath().resolve("captures");
        output = folder.resolve(name.replaceAll("[^A-Za-z0-9_.-]", "_") + ".mkv");
        List<String> command = new ArrayList<>(List.of(exe, "-hide_banner", "-loglevel", "warning", "-y",
            "-f", "rawvideo", "-pix_fmt", "rgba", "-s", width + "x" + height, "-framerate", Integer.toString(fps),
            "-i", "-",
            // The target's rows come bottom first, as a screenshot reads them.
            "-vf", "vflip",
            // A near-lossless intermediate on the GPU's encoder, so the CPU stays free for the game.
            "-c:v", "hevc_nvenc", "-preset", "p5", "-tune", "hq", "-rc", "constqp", "-qp", "12",
            "-pix_fmt", "yuv444p", output.toString()));
        try {
            Files.createDirectories(folder);
            ffmpeg = new ProcessBuilder(command).redirectErrorStream(true)
                .redirectOutput(folder.resolve(output.getFileName() + ".log").toFile()).start();
        } catch (IOException e) {
            RadianteClient.LOGGER.warn("[dev] recorder could not start ffmpeg", e);
            return;
        }
        queue = new ArrayBlockingQueue<>(24);
        OutputStream stdin = ffmpeg.getOutputStream();
        BlockingQueue<byte[]> frameQueue = queue;
        Process process = ffmpeg;
        writer = new Thread(() -> {
            try (stdin) {
                while (true) {
                    byte[] frame = frameQueue.take();
                    if (frame == END) {
                        break;
                    }
                    stdin.write(frame);
                    synchronized (FREE_FRAMES) {
                        FREE_FRAMES.push(frame);
                    }
                }
            } catch (IOException | InterruptedException e) {
                RadianteClient.LOGGER.warn("[dev] recorder writer stopped", e);
            }
            try {
                int exit = process.waitFor();
                RadianteClient.LOGGER.info("[dev] recording finished, ffmpeg exit {}", exit);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
        }, "Radiante dev recorder");
        writer.setDaemon(true);
        writer.start();
        frames = 0;
        delivered = 0;
        stopping = false;
        recording = true;
        RadianteClient.LOGGER.info("[dev] recording {} at {}x{} {} fps", output, width, height, fps);
    }

    /** Stops after the frames already asked for are written; the video is complete once ffmpeg exits. */
    static void stop() {
        if (!recording) {
            return;
        }
        recording = false;
        // The copies still in flight finish the video; the last one to arrive ends it.
        stopping = true;
        finishIfDone();
        RadianteClient.LOGGER.info("[dev] recording {} stopped after {} frames", output, frames);
    }

    /** At the end of every frame, GUI included: copies the finished frame for the video. */
    public static void frameEnd(RenderTarget target) {
        if (!recording) {
            return;
        }
        GpuTexture texture = target.getColorTexture();
        if (texture == null || target.width != width || target.height != height) {
            // A resized window would scramble the raw frames; the take is spoilt.
            RadianteClient.LOGGER.warn("[dev] window resized while recording; recording stopped");
            stop();
            return;
        }
        int size = width * height * texture.getFormat().blockSize();
        GpuBuffer buffer = FREE_BUFFERS.poll();
        if (buffer == null) {
            buffer = RenderSystem.getDevice().createBuffer(() -> "Radiante dev recorder",
                GpuBuffer.USAGE_MAP_READ | GpuBuffer.USAGE_COPY_DST, size);
        }
        GpuBuffer copy = buffer;
        BlockingQueue<byte[]> frameQueue = queue;
        frames++;
        RenderSystem.getDevice().createCommandEncoder().copyTextureToBuffer(texture, copy, 0L, () -> {
            byte[] frame;
            synchronized (FREE_FRAMES) {
                frame = FREE_FRAMES.poll();
            }
            if (frame == null || frame.length != size) {
                frame = new byte[size];
            }
            try (GpuBufferSlice.MappedView read = copy.map(true, false)) {
                read.data().get(0, frame, 0, size);
            }
            FREE_BUFFERS.push(copy);
            try {
                // Blocks the game when ffmpeg falls behind rather than dropping the frame.
                frameQueue.put(frame);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            delivered++;
            finishIfDone();
        }, 0);
    }
}
