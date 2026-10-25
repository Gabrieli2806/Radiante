#include "prism/render/textures.hpp"

#include "prism/render/emission.hpp"
#include "prism/render/opacity_micromaps.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>

namespace {

// Staged bytes that flush by themselves, so that a burst of uploads (a resource reload) does not stage without bound.
constexpr size_t FLUSH_THRESHOLD = 64ull * 1024 * 1024;
// Staging memory comes in blocks of this size, or of one upload's size when that is larger.
constexpr size_t STAGING_BLOCK_SIZE = 1024 * 1024;
// Staging blocks kept for reuse once their flush has finished; beyond this they are released.
constexpr size_t SPARE_STAGING_LIMIT = 32ull * 1024 * 1024;

// Everything that may read a texture after a flush: the hit shaders while tracing, the other modules' raster and
// compute passes, and the next flush's copies.
constexpr VkPipelineStageFlags2 TEXTURE_READERS =
    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR |
    VK_PIPELINE_STAGE_2_TRANSFER_BIT;

std::ostream &texturesErr() {
    return radiante::err() << "[Textures] ";
}

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Samplers are a limited device resource and every texture uses one of a handful of configurations, so textures share
// them. Never destroyed: at process exit (Linux runs static destructors after the Vulkan driver is gone) destroying
// them would call into unloaded driver code and crash the game on quit.
std::shared_ptr<gpu::Sampler> sharedSampler(const std::shared_ptr<gpu::Device> &device,
                                            VkFilter filter,
                                            VkSamplerMipmapMode mipmapMode,
                                            VkSamplerAddressMode addressMode) {
    struct Shared {
        VkDevice device;
        VkFilter filter;
        VkSamplerMipmapMode mipmapMode;
        VkSamplerAddressMode addressMode;
        std::shared_ptr<gpu::Sampler> sampler;
    };
    static std::mutex mutex;
    static auto &samplers = *new std::vector<Shared>();

    std::scoped_lock lock(mutex);
    for (const Shared &shared : samplers) {
        if (shared.device == device->vkDevice() && shared.filter == filter && shared.mipmapMode == mipmapMode &&
            shared.addressMode == addressMode) {
            return shared.sampler;
        }
    }
    auto sampler = gpu::Sampler::create(device, filter, mipmapMode, addressMode);
    samplers.push_back({device->vkDevice(), filter, mipmapMode, addressMode, sampler});
    return sampler;
}

} // namespace

Textures::Textures(std::shared_ptr<Framework> framework) : framework_(framework) {}

std::shared_ptr<Framework> Textures::framework() const {
    auto framework = framework_.lock();
    if (framework == nullptr) throw std::logic_error("Textures used after the renderer closed");
    return framework;
}

Textures::Entry &Textures::entry(uint32_t id, const char *action) {
    if (id < entries_.size()) return entries_[id];
    const std::string message =
        std::string("cannot ") + action + " texture " + std::to_string(id) + ", which was never allocated";
    texturesErr() << message << std::endl;
    throw std::out_of_range(message);
}

void Textures::bind(Framework &framework, uint32_t id, const Entry &entry) {
    if (auto pipeline = framework.pipeline(); pipeline != nullptr) {
        pipeline->bindTexture(entry.sampler, entry.image, static_cast<int>(id));
    }
}

// Java calls in from its own threads. Besides this object's lock every call takes the framework's frame lock, so a
// flush or a descriptor write never lands in the middle of a frame being recorded or of a rebuild.
uint32_t Textures::allocateTexture() {
    auto framework = this->framework();
    std::scoped_lock lock(mutex_, framework->recreateMtx());
    entries_.emplace_back();
    return static_cast<uint32_t>(entries_.size() - 1);
}

void Textures::initializeTexture(uint32_t id, uint32_t mipLevels, uint32_t width, uint32_t height, VkFormat format) {
    auto framework = this->framework();
    std::scoped_lock lock(mutex_, framework->recreateMtx());
    Entry &entry = this->entry(id, "make an image for");

    // An image of the same size, format and mip count is kept, and so is its descriptor: rewriting a bound descriptor
    // while frames that may sample it are in flight is what a resource reload or a resolution change used to crash
    // on, now and then. The next flush clears it, as it would a new image, unless copies are queued for it by then.
    const bool reuse = entry.image != nullptr && entry.image->width() == width && entry.image->height() == height &&
                       entry.image->mipLevels() == mipLevels && entry.image->vkFormat() == format;
    if (reuse) {
        entry.clear = true;
    } else {
        gpu::ImageDesc desc;
        desc.width = width;
        desc.height = height;
        desc.mipLevels = mipLevels;
        desc.format = format;
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        // The image it replaces retires once the frames in flight that may sample it are done.
        entry.image = gpu::DeviceLocalImage::create(framework->device(), framework->vma(), desc);
        entry.clear = false;
    }
    auto sampler = sharedSampler(framework->device(), VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                 VK_SAMPLER_ADDRESS_MODE_REPEAT);
    const bool rebind = !reuse || entry.sampler != sampler;
    entry.sampler = std::move(sampler);

    // Micromaps baked from the coverage of the image this id had no longer apply.
    omm::coverage::forget(id);

    // Minecraft closes and creates textures all the time: a font page or a GUI sprite dropped and a smaller one taking
    // its id in the same frame. Replayed into the new image, the copies queued for the old one would write outside it,
    // and the device would be lost a moment later.
    entry.copies.clear();
    entry.copyBlocks.clear();

    // A new image is undefined until written, yet it is bound right away and the tracer may sample it before its first
    // upload: a resource reload destroys every atlas frames before refilling it. Unless copies are queued for it by
    // then, the next flush clears it and puts it in the layout the shaders read.
    if (!entry.fresh) {
        entry.fresh = true;
        fresh_.push_back(id);
    }
    if (rebind) bind(*framework, id, entry);
}

void Textures::setSamplingMode(uint32_t id, VkFilter samplingMode, VkSamplerMipmapMode mipmapMode) {
    auto framework = this->framework();
    std::scoped_lock lock(mutex_, framework->recreateMtx());
    Entry &entry = this->entry(id, "set the filter of");
    // Without an image there is nothing to bind; initializeTexture picks the sampler.
    if (entry.sampler == nullptr) return;
    // Only a new filter picks another sampler, with the mipmap mode given along with it. A change of the mipmap mode
    // alone keeps the sampler, as it always has: applying it would change how the traced textures look.
    if (entry.sampler->vkSamplingMode() != samplingMode) {
        entry.sampler = sharedSampler(framework->device(), samplingMode, mipmapMode, entry.sampler->vkAddressMode());
    }
    bind(*framework, id, entry);
}

void Textures::queueUpload(uint8_t *srcPointer,
                           uint32_t srcSizeInBytes,
                           uint32_t srcRowPixels,
                           uint32_t dstId,
                           int srcOffsetX,
                           int srcOffsetY,
                           int dstOffsetX,
                           int dstOffsetY,
                           uint32_t width,
                           uint32_t height,
                           uint32_t level) {
    auto framework = this->framework();
    std::scoped_lock lock(mutex_, framework->recreateMtx());
    Entry &entry = this->entry(dstId, "upload into");
    if (srcPointer == nullptr || width == 0 || height == 0) return;
    if (entry.image == nullptr) {
        texturesErr() << "Dropped an upload into texture " << dstId << ", which has no image yet" << std::endl;
        return;
    }
    gpu::DeviceLocalImage &image = *entry.image;
    if (level >= image.mipLevels()) {
        texturesErr() << "Dropped an upload into texture " << dstId << " level " << level << ", which has "
                      << image.mipLevels() << " levels" << std::endl;
        return;
    }

    // A copy that reaches past the mip level's extent writes outside the image's memory. The driver is free to fault
    // on that, and it showed up as a device loss shortly after a world was entered.
    const uint32_t mipWidth = std::max(1u, image.width() >> level);
    const uint32_t mipHeight = std::max(1u, image.height() >> level);
    if (dstOffsetX < 0 || dstOffsetY < 0 || static_cast<uint64_t>(dstOffsetX) + width > mipWidth ||
        static_cast<uint64_t>(dstOffsetY) + height > mipHeight) {
        texturesErr() << "Dropped an out of bounds upload into texture " << dstId << " level " << level << ": "
                      << width << "x" << height << " at " << dstOffsetX << "," << dstOffsetY << " does not fit "
                      << mipWidth << "x" << mipHeight << std::endl;
        return;
    }

    const size_t texelBytes = gpu::formatToByte(image.vkFormat());
    // No row length means rows as wide as the copy, as in a Vulkan buffer to image copy.
    const size_t rowPixels = srcRowPixels != 0 ? srcRowPixels : width;
    const size_t rowBytes = rowPixels * texelBytes;
    const auto dropUnreadable = [&] {
        texturesErr() << "Dropped an upload into texture " << dstId << " that reads outside its source: " << width
                      << "x" << height << " at " << srcOffsetX << "," << srcOffsetY << " of " << srcSizeInBytes
                      << " bytes in rows of " << rowPixels << " texels" << std::endl;
    };
    if (srcOffsetX < 0 || srcOffsetY < 0 || width > rowPixels || (srcOffsetY > 0 && rowBytes > srcSizeInBytes)) {
        return dropUnreadable();
    }
    // The copy reads from the start of its first row to its last texel, and only that is staged: taking the whole
    // source made a glyph of eight by eight texels drag its entire sheet along, sixty four kilobytes at a time, once
    // per letter. A source that ends before the last texel would leave the GPU reading whatever follows it.
    const size_t skippedBytes = static_cast<size_t>(srcOffsetY) * rowBytes;
    const size_t readBytes = (static_cast<size_t>(srcOffsetX) + (height - 1) * rowPixels + width) * texelBytes;
    if (skippedBytes + readBytes > srcSizeInBytes) return dropUnreadable();
    // The block atlas's coverage, which cut-out micromaps are baked from, is taken from its uploads as they pass.
    if (texelBytes == 4) {
        omm::coverage::upload(dstId, level, srcPointer + skippedBytes + static_cast<size_t>(srcOffsetX) * texelBytes,
                              rowPixels, dstOffsetX, dstOffsetY, width, height);
    }
    // Buffer offsets of a copy are whole texels; staging allocations keep to 4 bytes as well.
    const auto [block, offset] = stage(srcPointer + skippedBytes, readBytes, std::lcm<size_t>(4, texelBytes));

    VkBufferImageCopy &copy = entry.copies.emplace_back();
    copy.bufferOffset = offset + static_cast<size_t>(srcOffsetX) * texelBytes;
    copy.bufferRowLength = static_cast<uint32_t>(rowPixels);
    copy.bufferImageHeight = 0;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
    copy.imageOffset = {dstOffsetX, dstOffsetY, 0};
    copy.imageExtent = {width, height, 1};
    entry.copyBlocks.push_back(block);
    if (!entry.queued) {
        entry.queued = true;
        queued_.push_back(dstId);
    }

    if (stagedBytes_ >= FLUSH_THRESHOLD) flush();
}

void Textures::performQueuedUpload() {
    auto framework = this->framework();
    std::scoped_lock lock(mutex_, framework->recreateMtx());
    flush();
}

void Textures::resetFrame() {
    auto framework = this->framework();
    std::scoped_lock lock(mutex_, framework->recreateMtx());
    collectFinishedFlushes();
}

void Textures::bindAllTextures() {
    auto framework = this->framework();
    std::scoped_lock lock(mutex_, framework->recreateMtx());
    for (uint32_t id = 0; id < entries_.size(); id++) {
        // Ids allocated but not given an image yet have nothing to bind.
        if (entries_[id].image != nullptr) bind(*framework, id, entries_[id]);
    }
}

std::shared_ptr<gpu::DeviceLocalImage> Textures::texture(uint32_t id) {
    auto framework = this->framework();
    std::scoped_lock lock(mutex_, framework->recreateMtx());
    return id < entries_.size() ? entries_[id].image : nullptr;
}

std::shared_ptr<gpu::Sampler> Textures::sampler(uint32_t id) {
    auto framework = this->framework();
    std::scoped_lock lock(mutex_, framework->recreateMtx());
    return id < entries_.size() ? entries_[id].sampler : nullptr;
}

// Only this object's lock: chunk builds ask for the emission while holding the chunks' lock, which a frame takes
// under the frame lock.
std::shared_ptr<Emission> Textures::emission() {
    if (!Renderer::options.collectChunkEmission) return nullptr;
    std::scoped_lock lock(mutex_);
    if (emission_ == nullptr) emission_ = Emission::create(weak_from_this());
    return emission_;
}

void Textures::releaseEmission() {
    std::scoped_lock lock(mutex_);
    if (emission_ != nullptr) {
        emission_->reset();
        emission_ = nullptr;
    }
}

std::pair<uint32_t, size_t> Textures::stage(const uint8_t *src, size_t size, size_t alignment) {
    size_t offset = staging_.empty() ? 0 : alignUp(staging_.back().used, alignment);
    if (staging_.empty() || offset + size > staging_.back().buffer->size()) {
        staging_.push_back({takeStagingBuffer(size), 0});
        offset = 0;
    }
    StagingBlock &block = staging_.back();
    std::memcpy(static_cast<uint8_t *>(block.buffer->mappedPtr()) + offset, src, size);
    block.used = offset + size;
    stagedBytes_ += size;
    return {static_cast<uint32_t>(staging_.size() - 1), offset};
}

std::shared_ptr<gpu::HostVisibleBuffer> Textures::takeStagingBuffer(size_t size) {
    // The smallest spare buffer that holds it.
    size_t best = spareStaging_.size();
    for (size_t i = 0; i < spareStaging_.size(); i++) {
        const size_t capacity = spareStaging_[i]->size();
        if (capacity >= size && (best == spareStaging_.size() || capacity < spareStaging_[best]->size())) best = i;
    }
    if (best < spareStaging_.size()) {
        std::swap(spareStaging_[best], spareStaging_.back());
        auto buffer = std::move(spareStaging_.back());
        spareStaging_.pop_back();
        spareStagingBytes_ -= buffer->size();
        return buffer;
    }
    auto framework = this->framework();
    return gpu::HostVisibleBuffer::create(framework->vma(), framework->device(),
                                          alignUp(std::max(size, STAGING_BLOCK_SIZE), STAGING_BLOCK_SIZE),
                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
}

void Textures::keepStagingBuffer(std::shared_ptr<gpu::HostVisibleBuffer> buffer) {
    // Over the limit it is released, which retires it.
    if (spareStagingBytes_ + buffer->size() > SPARE_STAGING_LIMIT) return;
    spareStagingBytes_ += buffer->size();
    spareStaging_.push_back(std::move(buffer));
}

void Textures::collectFinishedFlushes() {
    if (inFlight_.empty()) return;
    const VkDevice device = framework()->device()->vkDevice();
    for (size_t i = 0; i < inFlight_.size();) {
        Flush &done = inFlight_[i];
        if (vkGetFenceStatus(device, done.fence->vkFence()) != VK_SUCCESS) {
            i++;
            continue;
        }
        for (auto &buffer : done.staging) keepStagingBuffer(std::move(buffer));
        done.staging.clear();
        done.images.clear();
        idle_.push_back(std::move(done));
        if (i + 1 < inFlight_.size()) inFlight_[i] = std::move(inFlight_.back());
        inFlight_.pop_back();
    }
}

void Textures::flush() {
    collectFinishedFlushes();
    if (queued_.empty() && fresh_.empty()) return;

    // Made before anything queued is taken, so that a failure here loses nothing.
    auto framework = this->framework();
    const std::shared_ptr<gpu::Device> device = framework->device();
    Flush batch;
    if (!idle_.empty()) {
        batch = std::move(idle_.back());
        idle_.pop_back();
        vkResetFences(device->vkDevice(), 1, &batch.fence->vkFence());
        batch.commandBuffer->reset();
    } else {
        batch.commandBuffer = gpu::CommandBuffer::create(device, framework->mainCommandPool());
        batch.fence = gpu::Fence::create(device);
    }

    uploadIds_.clear();
    for (uint32_t id : queued_) {
        Entry &entry = entries_[id];
        entry.queued = false;
        // A new image since dropped the copies of some.
        if (!entry.copies.empty()) uploadIds_.push_back(id);
    }
    queued_.clear();
    clearIds_.clear();
    for (uint32_t id : fresh_) {
        Entry &entry = entries_[id];
        entry.fresh = false;
        // One with copies queued is about to be written and put in the shaders' layout anyway. A kept image still
        // holds the texels of the texture that had its id before.
        const bool clear = entry.clear || entry.image->state().layout == VK_IMAGE_LAYOUT_UNDEFINED;
        entry.clear = false;
        if (entry.copies.empty() && clear) clearIds_.push_back(id);
    }
    fresh_.clear();

    if (uploadIds_.empty() && clearIds_.empty()) {
        idle_.push_back(std::move(batch));
        for (auto &block : staging_) keepStagingBuffer(std::move(block.buffer));
        staging_.clear();
        stagedBytes_ = 0;
        return;
    }

    gpu::CommandBuffer &commands = *batch.commandBuffer;
    const VkCommandBuffer commandBuffer = commands.vkCommandBuffer();
    commands.begin();

    // Copies only write part of an image: the rest is kept. A cleared image has nothing to keep.
    uses_.clear();
    for (uint32_t id : clearIds_) uses_.emplace_back(entries_[id].image, gpu::ImageUsage::TransferDst, 0, true);
    for (uint32_t id : uploadIds_) uses_.emplace_back(entries_[id].image, gpu::ImageUsage::TransferDst);
    commands.sync(uses_);

    const VkClearColorValue transparentBlack{};
    for (uint32_t id : clearIds_) {
        gpu::DeviceLocalImage &image = *entries_[id].image;
        const VkImageSubresourceRange range = image.subresources();
        vkCmdClearColorImage(commandBuffer, image.vkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transparentBlack, 1,
                             &range);
    }
    for (uint32_t id : uploadIds_) {
        Entry &entry = entries_[id];
        const size_t count = entry.copies.size();
        for (size_t first = 0; first < count;) {
            const uint32_t block = entry.copyBlocks[first];
            size_t end = first + 1;
            while (end < count && entry.copyBlocks[end] == block) end++;
            // The copies of one image that read the next staging block are a command of their own, which must not
            // overtake the one before it where the two overlap.
            if (first > 0) {
                const gpu::ImageUse again(entry.image, gpu::ImageUsage::TransferDst);
                commands.sync(std::span<const gpu::ImageUse>(&again, 1));
            }
            vkCmdCopyBufferToImage(commandBuffer, staging_[block].buffer->vkBuffer(), entry.image->vkImage(),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(end - first),
                                   entry.copies.data() + first);
            first = end;
        }
        entry.copies.clear();
        entry.copyBlocks.clear();
    }

    uses_.clear();
    for (uint32_t id : clearIds_) uses_.emplace_back(entries_[id].image, gpu::ImageUsage::Sampled, TEXTURE_READERS);
    for (uint32_t id : uploadIds_) uses_.emplace_back(entries_[id].image, gpu::ImageUsage::Sampled, TEXTURE_READERS);
    commands.sync(uses_);
    commands.end();

    for (uint32_t id : clearIds_) batch.images.push_back(entries_[id].image);
    for (uint32_t id : uploadIds_) batch.images.push_back(entries_[id].image);
    for (auto &block : staging_) {
        block.buffer->flush();
        batch.staging.push_back(std::move(block.buffer));
    }
    staging_.clear();
    stagedBytes_ = 0;

    commands.submitMainQueueIndividual(device, batch.fence);
    inFlight_.push_back(std::move(batch));
}
