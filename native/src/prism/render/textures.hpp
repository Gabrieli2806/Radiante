#pragma once

#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

class Framework;
class Emission;

// Minecraft's textures, mirrored under the ids Java hands out and bound into the world modules' bindless tables.
//
// Uploads are staged in host memory as they arrive. A flush records them, with the clears of the images made since the
// last one, into a command buffer of its own and submits it at once: the framework flushes at the start of every frame
// and before the world is traced, Java flushes when it needs the texels on the GPU, and 64 MiB of staged texels flush
// by themselves. A flush keeps its command buffer, its staging memory and the images it writes until its fence has
// signalled; then the command buffer and the staging memory are reused.
class Textures : public SharedObject<Textures> {
  public:
    explicit Textures(std::shared_ptr<Framework> framework);

    uint32_t allocateTexture();
    // Gives an allocated id a new image, cleared to transparent black unless the next flush copies into it. Copies
    // still queued for the image it replaces are dropped: they were sized for that one.
    void initializeTexture(uint32_t id, uint32_t mipLevels, uint32_t width, uint32_t height, VkFormat format);
    void setSamplingMode(uint32_t id, VkFilter samplingMode, VkSamplerMipmapMode mipmapMode);
    // Queues the copy of a width x height block of texels, read at (srcOffsetX, srcOffsetY) from rows of srcRowPixels
    // texels at srcPointer (srcSizeInBytes long), to (dstOffsetX, dstOffsetY) of mip level `level`. A copy that would
    // write outside the image or read outside its source is dropped.
    void queueUpload(uint8_t *srcPointer,
                     uint32_t srcSizeInBytes,
                     uint32_t srcRowPixels,
                     uint32_t dstId,
                     int srcOffsetX,
                     int srcOffsetY,
                     int dstOffsetX,
                     int dstOffsetY,
                     uint32_t width,
                     uint32_t height,
                     uint32_t level);
    // Submits everything queued.
    void performQueuedUpload();
    // A frame slot opened: flushes the GPU has finished give their command buffers and staging memory back.
    void resetFrame();
    // Binds every texture again, into the world modules a rebuild has just made.
    void bindAllTextures();

    // Null for an id that is not allocated or has no image yet.
    std::shared_ptr<gpu::DeviceLocalImage> texture(uint32_t id);
    std::shared_ptr<gpu::Sampler> sampler(uint32_t id);

    // Where the emissive texels of each texture are, for the light lists of chunk and entity builds; null while chunk
    // emission is not collected.
    std::shared_ptr<Emission> emission();
    void releaseEmission();

  private:
    struct Entry {
        std::shared_ptr<gpu::DeviceLocalImage> image;
        std::shared_ptr<gpu::Sampler> sampler;
        // The copies queued since the last flush, in order, and the staging block each one reads.
        std::vector<VkBufferImageCopy> copies;
        std::vector<uint32_t> copyBlocks;
        bool queued = false; // listed in queued_
        bool fresh = false;  // listed in fresh_
    };

    struct StagingBlock {
        std::shared_ptr<gpu::HostVisibleBuffer> buffer;
        size_t used = 0;
    };

    struct Flush {
        std::shared_ptr<gpu::CommandBuffer> commandBuffer;
        std::shared_ptr<gpu::Fence> fence;
        std::vector<std::shared_ptr<gpu::HostVisibleBuffer>> staging;
        std::vector<std::shared_ptr<gpu::DeviceLocalImage>> images;
    };

    std::shared_ptr<Framework> framework() const;
    Entry &entry(uint32_t id, const char *action);
    void bind(Framework &framework, uint32_t id, const Entry &entry);
    // Copies `size` bytes to staging memory at an `alignment` aligned offset. Returns the staging block and the offset.
    std::pair<uint32_t, size_t> stage(const uint8_t *src, size_t size, size_t alignment);
    std::shared_ptr<gpu::HostVisibleBuffer> takeStagingBuffer(size_t size);
    void keepStagingBuffer(std::shared_ptr<gpu::HostVisibleBuffer> buffer);
    void collectFinishedFlushes();
    void flush();

    std::weak_ptr<Framework> framework_;
    // Recursive: bind() calls into the world modules while it is held.
    std::recursive_mutex mutex_;
    std::vector<Entry> entries_;
    std::shared_ptr<Emission> emission_;

    // Ids with copies queued since the last flush, ids given an image since then, and the staging memory the queued
    // copies read (the last block is the one being filled).
    std::vector<uint32_t> queued_;
    std::vector<uint32_t> fresh_;
    std::vector<StagingBlock> staging_;
    size_t stagedBytes_ = 0;

    std::vector<Flush> inFlight_;
    std::vector<Flush> idle_;
    std::vector<std::shared_ptr<gpu::HostVisibleBuffer>> spareStaging_;
    size_t spareStagingBytes_ = 0;

    // Reused by every flush.
    std::vector<uint32_t> uploadIds_;
    std::vector<uint32_t> clearIds_;
    std::vector<gpu::ImageUse> uses_;
};
