package com.g2806.radiante.client.pipeline;

import com.g2806.radiante.client.pipeline.config.AttributeConfig;
import com.g2806.radiante.client.pipeline.config.ImageConfig;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.lwjgl.system.MemoryUtil;

/**
 * A sorted pipeline written into native memory the way the renderer reads it ({@link Pipeline#buildNative(long)},
 * {@link Pipeline#prewarmNative(long)}): the modules' names, the images each reads and writes, and their attributes.
 */
final class NativePipelineParams {

    private NativePipelineParams() {
    }

    /** Lays the pipeline out as the native side reads it, for a build or (prewarmOnly) a prewarm of one. */
    static boolean buildNative(List<Module> modules, List<Integer> formats,
                                       Map<ImageConfig, Integer> imgMap, List<List<AttributeConfig>> moduleAttributes,
                                       boolean prewarmOnly) {
        List<ByteBuffer> allocatedBuffers = new ArrayList<>();

        try {
            int moduleCount = modules.size();

            ByteBuffer formatBuffer = allocAndTrack(allocatedBuffers, formats.size() * 4);
            for (Integer fmt : formats) {
                formatBuffer.putInt(fmt);
            }
            formatBuffer.flip();

            ByteBuffer namesPtrBuffer = allocAndTrack(allocatedBuffers, moduleCount * 8);
            ByteBuffer inputsPtrBuffer = allocAndTrack(allocatedBuffers, moduleCount * 8);
            ByteBuffer outputsPtrBuffer = allocAndTrack(allocatedBuffers, moduleCount * 8);
            ByteBuffer attributeCountsBuffer = allocAndTrack(allocatedBuffers, moduleCount * 4);
            ByteBuffer attributesPtrBuffer = allocAndTrack(allocatedBuffers, moduleCount * 8);

            for (int i = 0; i < moduleCount; i++) {
                Module module = modules.get(i);

                byte[] nameBytes = module.name.getBytes(java.nio.charset.StandardCharsets.UTF_8);
                ByteBuffer nameBuffer = allocAndTrack(allocatedBuffers, nameBytes.length + 1);
                nameBuffer.put(nameBytes);
                nameBuffer.put((byte) 0);
                nameBuffer.flip();
                namesPtrBuffer.putLong(MemoryUtil.memAddress(nameBuffer));

                List<ImageConfig> inputs = module.inputImageConfigs;
                ByteBuffer inputIndices = allocAndTrack(allocatedBuffers, inputs.size() * 4);
                for (ImageConfig inConfig : inputs) {
                    inputIndices.putInt(imgMap.get(inConfig));
                }
                inputIndices.flip();
                inputsPtrBuffer.putLong(MemoryUtil.memAddress(inputIndices));

                List<ImageConfig> outputs = module.outputImageConfigs;
                ByteBuffer outputIndices = allocAndTrack(allocatedBuffers, outputs.size() * 4);
                for (ImageConfig outConfig : outputs) {
                    outputIndices.putInt(imgMap.getOrDefault(outConfig, -1));
                }
                outputIndices.flip();
                outputsPtrBuffer.putLong(MemoryUtil.memAddress(outputIndices));

                List<AttributeConfig> attrs = moduleAttributes.get(i);
                attributeCountsBuffer.putInt(attrs.size());

                if (!attrs.isEmpty()) {
                    ByteBuffer attrKVPointers = allocAndTrack(allocatedBuffers,
                            attrs.size() * 2 * 8);
                    for (AttributeConfig attr : attrs) {
                        byte[] kBytes = attr.name.getBytes(StandardCharsets.UTF_8);
                        byte[] vBytes = (attr.value != null ? attr.value : "").getBytes(
                                StandardCharsets.UTF_8);

                        ByteBuffer kBuf = allocAndTrack(allocatedBuffers, kBytes.length + 1);
                        kBuf.put(kBytes).put((byte) 0).flip();

                        ByteBuffer vBuf = allocAndTrack(allocatedBuffers, vBytes.length + 1);
                        vBuf.put(vBytes).put((byte) 0).flip();

                        attrKVPointers.putLong(MemoryUtil.memAddress(kBuf));
                        attrKVPointers.putLong(MemoryUtil.memAddress(vBuf));
                    }
                    attrKVPointers.flip();
                    attributesPtrBuffer.putLong(MemoryUtil.memAddress(attrKVPointers));
                } else {
                    attributesPtrBuffer.putLong(0);
                }
            }

            namesPtrBuffer.flip();
            inputsPtrBuffer.flip();
            outputsPtrBuffer.flip();
            attributeCountsBuffer.flip();
            attributesPtrBuffer.flip();

            ByteBuffer params = allocAndTrack(allocatedBuffers, 56);

            params.putInt(moduleCount);
            params.putInt(0); // Padding

            params.putLong(MemoryUtil.memAddress(namesPtrBuffer));
            params.putLong(MemoryUtil.memAddress(formatBuffer));
            params.putLong(MemoryUtil.memAddress(inputsPtrBuffer));
            params.putLong(MemoryUtil.memAddress(outputsPtrBuffer));
            params.putLong(MemoryUtil.memAddress(attributeCountsBuffer));
            params.putLong(MemoryUtil.memAddress(attributesPtrBuffer));

            params.flip();

            if (prewarmOnly) {
                return Pipeline.prewarmNative(MemoryUtil.memAddress(params));
            }
            Pipeline.buildNative(MemoryUtil.memAddress(params));
            return true;
        } finally {
            for (ByteBuffer buffer : allocatedBuffers) {
                MemoryUtil.memFree(buffer);
            }
        }
    }

    private static ByteBuffer allocAndTrack(List<ByteBuffer> allocatedBuffers, int size) {
        ByteBuffer buffer = MemoryUtil.memAlloc(size);
        allocatedBuffers.add(buffer);
        return buffer;
    }
}
