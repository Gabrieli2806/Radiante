/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * MODIFICATIONS and INTEGRATION:
 *
 * Copyright (c) 2026 Radiance
 *
 * This file has been modified from its original version to be integrated into
 * Radiance Mod.
 *
 * Modifications include:
 * - Integration with Radiance Mod's vulkan rendering system.
 * - Refactoring of APIs, introducing RAII and supporting shared_ptr.
 *
 * These modifications are licensed under the GNU General Public License
 * as published by the Free Software Foundation; either version 3 of the License,
 * or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "prism/render/modules/world/nrd/nrd_wrapper.hpp"

#include "prism/render/render_framework.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

// nrd::Format, in its order.
constexpr auto kVkFormats = std::to_array<VkFormat>({
    VK_FORMAT_R8_UNORM,
    VK_FORMAT_R8_SNORM,
    VK_FORMAT_R8_UINT,
    VK_FORMAT_R8_SINT,
    VK_FORMAT_R8G8_UNORM,
    VK_FORMAT_R8G8_SNORM,
    VK_FORMAT_R8G8_UINT,
    VK_FORMAT_R8G8_SINT,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R8G8B8A8_SNORM,
    VK_FORMAT_R8G8B8A8_UINT,
    VK_FORMAT_R8G8B8A8_SINT,
    VK_FORMAT_R8G8B8A8_SRGB,
    VK_FORMAT_R16_UNORM,
    VK_FORMAT_R16_SNORM,
    VK_FORMAT_R16_UINT,
    VK_FORMAT_R16_SINT,
    VK_FORMAT_R16_SFLOAT,
    VK_FORMAT_R16G16_UNORM,
    VK_FORMAT_R16G16_SNORM,
    VK_FORMAT_R16G16_UINT,
    VK_FORMAT_R16G16_SINT,
    VK_FORMAT_R16G16_SFLOAT,
    VK_FORMAT_R16G16B16A16_UNORM,
    VK_FORMAT_R16G16B16A16_SNORM,
    VK_FORMAT_R16G16B16A16_UINT,
    VK_FORMAT_R16G16B16A16_SINT,
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_R32_UINT,
    VK_FORMAT_R32_SINT,
    VK_FORMAT_R32_SFLOAT,
    VK_FORMAT_R32G32_UINT,
    VK_FORMAT_R32G32_SINT,
    VK_FORMAT_R32G32_SFLOAT,
    VK_FORMAT_R32G32B32_UINT,
    VK_FORMAT_R32G32B32_SINT,
    VK_FORMAT_R32G32B32_SFLOAT,
    VK_FORMAT_R32G32B32A32_UINT,
    VK_FORMAT_R32G32B32A32_SINT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    VK_FORMAT_A2B10G10R10_UINT_PACK32,
    VK_FORMAT_B10G11R11_UFLOAT_PACK32,
    VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
});
static_assert(kVkFormats.size() == size_t(nrd::Format::MAX_NUM));

uint16_t divideUp(uint16_t size, uint16_t factor) {
    return uint16_t((size + factor - 1) / factor);
}

void check(VkResult result, const char *what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("NRD: ") + what + " failed (VkResult " + std::to_string(result) + ")");
    }
}

// Shader modules live only until the pipelines are made from them.
struct ShaderModules {
    VkDevice device;
    std::vector<VkShaderModule> modules;

    ~ShaderModules() {
        for (VkShaderModule module : modules) vkDestroyShaderModule(device, module, nullptr);
    }
};

} // namespace

void NrdWrapper::InstanceDeleter::operator()(nrd::Instance *instance) const {
    nrd::DestroyInstance(*instance);
}

void NrdWrapper::init(std::shared_ptr<Framework> framework,
                      uint16_t width,
                      uint16_t height,
                      std::span<const nrd::Denoiser> denoisers) {
    device_ = framework->device();
    vma_ = framework->vma();
    frameSlots_ = std::max(1u, framework->swapchain()->imageCount());

    std::vector<nrd::DenoiserDesc> denoiserDescs;
    denoiserDescs.reserve(denoisers.size());
    for (nrd::Denoiser denoiser : denoisers) denoiserDescs.push_back({nrd::Identifier(denoiser), denoiser});
    nrd::InstanceCreationDesc creation = {};
    creation.denoisers = denoiserDescs.data();
    creation.denoisersNum = static_cast<uint32_t>(denoiserDescs.size());

    nrd::Instance *instance = nullptr;
    const nrd::Result result = nrd::CreateInstance(creation, instance);
    if (result != nrd::Result::SUCCESS) {
        throw std::runtime_error("NRD: the denoiser instance could not be made (result " +
                                 std::to_string(static_cast<uint32_t>(result)) + ")");
    }
    instance_.reset(instance);
    desc_ = nrd::GetInstanceDesc(*instance_);
    // The pipeline layouts give NRD's two register spaces the descriptor set numbers 0 and 1.
    if (desc_->resourcesSpaceIndex > 1 || desc_->constantBufferAndSamplersSpaceIndex > 1 ||
        desc_->resourcesSpaceIndex == desc_->constantBufferAndSamplersSpaceIndex) {
        throw std::runtime_error("NRD: register spaces other than 0 and 1 are not supported");
    }

    createTextures(width, height);
    clearTextures(framework);
    createSharedLayout();
    createPipelines();

    const VkDeviceSize alignment =
        std::max<VkDeviceSize>(1, framework->physicalDevice()->properties().limits.minUniformBufferOffsetAlignment);
    constantSlotSize_ =
        (std::max<VkDeviceSize>(desc_->constantBufferMaxDataSize, 1) + alignment - 1) / alignment * alignment;
    // NRD's own bound on the dispatches one frame makes; denoise() grows the ring should a frame need more.
    createConstantRing(std::max(1u, desc_->descriptorPoolDesc.setsMaxNum));

    size_t mostTextures = 0;
    for (uint32_t p = 0; p < desc_->pipelinesNum; p++) {
        size_t textures = 0;
        for (uint32_t r = 0; r < desc_->pipelines[p].resourceRangesNum; r++) {
            textures += desc_->pipelines[p].resourceRanges[r].descriptorsNum;
        }
        mostTextures = std::max(mostTextures, textures);
    }
    writes_.reserve(mostTextures);
    imageInfos_.reserve(mostTextures);
    userTextureUses_.reserve(size_t(nrd::ResourceType::MAX_NUM));
}

NrdWrapper::~NrdWrapper() {
    if (device_ == nullptr) return;
    retireConstantRing();
    gpu::retire::later([device = device_, pipelines = std::move(pipelines_), sharedLayout = sharedLayout_] {
        const VkDevice vkDevice = device->vkDevice();
        for (const Pipeline &pipeline : pipelines) {
            vkDestroyPipeline(vkDevice, pipeline.pipeline, nullptr);
            vkDestroyPipelineLayout(vkDevice, pipeline.layout, nullptr);
            vkDestroyDescriptorSetLayout(vkDevice, pipeline.textureLayout, nullptr);
        }
        vkDestroyDescriptorSetLayout(vkDevice, sharedLayout, nullptr);
    });
}

void NrdWrapper::createTextures(uint16_t width, uint16_t height) {
    auto make = [&](const nrd::TextureDesc &texture) {
        gpu::ImageDesc image;
        image.width = divideUp(width, texture.downsampleFactor);
        image.height = divideUp(height, texture.downsampleFactor);
        image.format = kVkFormats.at(size_t(texture.format));
        image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        return gpu::DeviceLocalImage::create(device_, vma_, image);
    };
    permanentTextures_.reserve(desc_->permanentPoolSize);
    for (uint32_t i = 0; i < desc_->permanentPoolSize; i++) permanentTextures_.push_back(make(desc_->permanentPool[i]));
    transientTextures_.reserve(desc_->transientPoolSize);
    for (uint32_t i = 0; i < desc_->transientPoolSize; i++) transientTextures_.push_back(make(desc_->transientPool[i]));
}

// NRD's textures start as zeros, not as whatever memory they were given, and stay in GENERAL from then on.
void NrdWrapper::clearTextures(const std::shared_ptr<Framework> &framework) {
    std::vector<gpu::ImageUse> uses;
    uses.reserve(permanentTextures_.size() + transientTextures_.size());
    auto useAll = [&](gpu::ImageUsage usage, VkPipelineStageFlags2 stages, bool discard) {
        uses.clear();
        for (const auto &texture : permanentTextures_) uses.emplace_back(texture, usage, stages, discard);
        for (const auto &texture : transientTextures_) uses.emplace_back(texture, usage, stages, discard);
    };

    auto commandPool = gpu::CommandPool::create(framework->physicalDevice(), device_);
    auto commandBuffer = gpu::CommandBuffer::create(device_, commandPool);
    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    useAll(gpu::ImageUsage::TransferDst, 0, true);
    commandBuffer->sync(uses);
    const VkClearColorValue zero = {};
    for (const gpu::ImageUse &use : uses) {
        vkCmdClearColorImage(commandBuffer->vkCommandBuffer(), use.image->vkImage(),
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &gpu::wholeColorSubresourceRange);
    }
    useAll(gpu::ImageUsage::General, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, false);
    commandBuffer->sync(uses);
    commandBuffer->end();

    auto fence = gpu::Fence::create(device_);
    commandBuffer->submitMainQueueIndividual(device_, fence);
    check(vkWaitForFences(device_->vkDevice(), 1, &fence->vkFence(), VK_TRUE, UINT64_MAX), "clearing its textures");
}

// The set every pipeline shares: NRD's samplers, immutable, and the constant buffer, bound by dynamic offset.
void NrdWrapper::createSharedLayout() {
    const nrd::SPIRVBindingOffsets &offsets = nrd::GetLibraryDesc()->spirvBindingOffsets;

    std::vector<VkSampler> vkSamplers;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    vkSamplers.reserve(desc_->samplersNum);
    bindings.reserve(desc_->samplersNum + 1);
    for (uint32_t i = 0; i < desc_->samplersNum; i++) {
        const VkFilter filter = desc_->samplers[i] == nrd::Sampler::LINEAR_CLAMP ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        samplers_.push_back(gpu::Sampler::create(device_, filter, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                                 VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
        vkSamplers.push_back(samplers_.back()->vkSamper());
        bindings.push_back({offsets.samplerOffset + desc_->samplersBaseRegisterIndex + i, VK_DESCRIPTOR_TYPE_SAMPLER, 1,
                            VK_SHADER_STAGE_COMPUTE_BIT, &vkSamplers.back()});
    }
    bindings.push_back({offsets.constantBufferOffset + desc_->constantBufferRegisterIndex,
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});

    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(device_->vkDevice(), &info, nullptr, &sharedLayout_),
          "making its sampler and constant layout");
}

// NRD describes each pipeline's textures and ships its SPIR-V; the layouts rebuild exactly the bindings its shaders
// were compiled with. All pipelines are made in one call, through the device's pipeline cache.
void NrdWrapper::createPipelines() {
    const nrd::SPIRVBindingOffsets &offsets = nrd::GetLibraryDesc()->spirvBindingOffsets;
    const VkDevice vkDevice = device_->vkDevice();
    const uint32_t count = desc_->pipelinesNum;

    pipelines_.resize(count);
    ShaderModules modules{vkDevice, std::vector<VkShaderModule>(count, VK_NULL_HANDLE)};
    std::vector<VkComputePipelineCreateInfo> infos(count);
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t p = 0; p < count; p++) {
        const nrd::PipelineDesc &nrdPipeline = desc_->pipelines[p];
        Pipeline &pipeline = pipelines_[p];

        bindings.clear();
        for (uint32_t r = 0; r < nrdPipeline.resourceRangesNum; r++) {
            const nrd::ResourceRangeDesc &range = nrdPipeline.resourceRanges[r];
            const bool storage = range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
            const uint32_t first = (storage ? offsets.storageTextureAndBufferOffset : offsets.textureOffset) +
                                   desc_->resourcesBaseRegisterIndex;
            for (uint32_t i = 0; i < range.descriptorsNum; i++) {
                bindings.push_back({first + i,
                                    storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                                    VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
            }
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        check(vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &pipeline.textureLayout),
              "making a texture layout");

        std::array<VkDescriptorSetLayout, 2> setLayouts{};
        setLayouts[desc_->resourcesSpaceIndex] = pipeline.textureLayout;
        setLayouts[desc_->constantBufferAndSamplersSpaceIndex] = sharedLayout_;
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        check(vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipeline.layout),
              "making a pipeline layout");

        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = static_cast<size_t>(nrdPipeline.computeShaderSPIRV.size);
        moduleInfo.pCode = static_cast<const uint32_t *>(nrdPipeline.computeShaderSPIRV.bytecode);
        check(vkCreateShaderModule(vkDevice, &moduleInfo, nullptr, &modules.modules[p]), "loading a shader");

        VkComputePipelineCreateInfo &info = infos[p];
        info = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = modules.modules[p];
        info.stage.pName = desc_->shaderEntryPoint;
        info.layout = pipeline.layout;
    }
    if (count == 0) return;

    std::vector<VkPipeline> made(count, VK_NULL_HANDLE);
    const VkResult result =
        vkCreateComputePipelines(vkDevice, device_->pipelineCache(), count, infos.data(), nullptr, made.data());
    for (uint32_t p = 0; p < count; p++) pipelines_[p].pipeline = made[p];
    check(result, "building its compute pipelines");
}

void NrdWrapper::createConstantRing(uint32_t slotsPerFrame) {
    const VkDevice vkDevice = device_->vkDevice();
    ConstantRing ring;
    ring.slotsPerFrame = slotsPerFrame;
    ring.buffer = gpu::HostVisibleBuffer::create(vma_, device_,
                                                 static_cast<size_t>(constantSlotSize_ * slotsPerFrame * frameSlots_),
                                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    const std::array<VkDescriptorPoolSize, 2> sizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
        {VK_DESCRIPTOR_TYPE_SAMPLER, desc_->samplersNum},
    }};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = desc_->samplersNum > 0 ? 2 : 1;
    poolInfo.pPoolSizes = sizes.data();
    check(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &ring.pool), "making its descriptor pool");

    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = ring.pool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &sharedLayout_;
    const VkResult allocated = vkAllocateDescriptorSets(vkDevice, &allocateInfo, &ring.set);
    if (allocated != VK_SUCCESS) {
        vkDestroyDescriptorPool(vkDevice, ring.pool, nullptr);
        check(allocated, "allocating its sampler and constant set");
    }

    const VkDescriptorBufferInfo bufferInfo{ring.buffer->vkBuffer(), 0, constantSlotSize_};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = ring.set;
    write.dstBinding =
        nrd::GetLibraryDesc()->spirvBindingOffsets.constantBufferOffset + desc_->constantBufferRegisterIndex;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(vkDevice, 1, &write, 0, nullptr);

    retireConstantRing();
    constants_ = std::move(ring);
}

void NrdWrapper::retireConstantRing() {
    if (constants_.pool != VK_NULL_HANDLE) {
        gpu::retire::later([device = device_, pool = constants_.pool] {
            vkDestroyDescriptorPool(device->vkDevice(), pool, nullptr);
        });
    }
    constants_ = {}; // the buffer retires itself
}

void NrdWrapper::setCommonSettings(const nrd::CommonSettings &settings) {
    // NRD keeps the settings even when it finds fault with them, so the frame goes on; say so once.
    const nrd::Result result = nrd::SetCommonSettings(*instance_, settings);
    if (result != nrd::Result::SUCCESS && !commonSettingsRejected_) {
        commonSettingsRejected_ = true;
        radiante::err() << "[NRD] frame settings rejected (result " << static_cast<uint32_t>(result) << ")"
                        << std::endl;
    }
}

void NrdWrapper::setReblurSettings(nrd::Denoiser denoiser, const nrd::ReblurSettings &settings) {
    setDenoiserSettings(denoiser, &settings);
}

void NrdWrapper::setRelaxSettings(nrd::Denoiser denoiser, const nrd::RelaxSettings &settings) {
    setDenoiserSettings(denoiser, &settings);
}

void NrdWrapper::setDenoiserSettings(nrd::Denoiser denoiser, const void *settings) {
    const nrd::Result result = nrd::SetDenoiserSettings(*instance_, nrd::Identifier(denoiser), settings);
    if (result != nrd::Result::SUCCESS) {
        radiante::err() << "[NRD] " << nrd::GetDenoiserString(denoiser) << " settings rejected (result "
                        << static_cast<uint32_t>(result) << ")" << std::endl;
    }
}

const std::shared_ptr<gpu::DeviceLocalImage> &NrdWrapper::texture(const nrd::ResourceDesc &resource,
                                                                  const NrdUserTextures &userTextures) const {
    switch (resource.type) {
        case nrd::ResourceType::PERMANENT_POOL: return permanentTextures_.at(resource.indexInPool);
        case nrd::ResourceType::TRANSIENT_POOL: return transientTextures_.at(resource.indexInPool);
        default: break;
    }
    const auto &image = userTextures.at(size_t(resource.type));
    if (image == nullptr) {
        throw std::logic_error(std::string("NRD needs the ") + nrd::GetResourceTypeString(resource.type) +
                               " texture, which was not given");
    }
    return image;
}

void NrdWrapper::denoise(gpu::CommandBuffer &commandBuffer,
                         uint32_t frameSlot,
                         nrd::Denoiser denoiser,
                         const NrdUserTextures &userTextures) {
    const nrd::Identifier identifier = nrd::Identifier(denoiser);
    const nrd::DispatchDesc *dispatches = nullptr;
    uint32_t dispatchCount = 0;
    nrd::GetComputeDispatches(*instance_, &identifier, 1, dispatches, dispatchCount);
    if (dispatchCount == 0) return;

    // A dispatch either brings constants of its own or reuses the previous dispatch's, which NRD tells it.
    auto bringsConstants = [&](const nrd::DispatchDesc &dispatch, bool constantsBound) {
        return desc_->pipelines[dispatch.pipelineIndex].hasConstantData && dispatch.constantBufferDataSize != 0 &&
               (!dispatch.constantBufferDataMatchesPreviousDispatch || !constantsBound);
    };
    uint32_t constantSlots = 0;
    for (uint32_t d = 0; d < dispatchCount; d++) {
        if (bringsConstants(dispatches[d], constantSlots != 0)) constantSlots++;
    }
    if (constantSlots > constants_.slotsPerFrame) {
        radiante::out() << "[NRD] " << constantSlots << " dispatches with constants in one frame, room for "
                        << constants_.slotsPerFrame << "; growing" << std::endl;
        createConstantRing(std::max(constantSlots, constants_.slotsPerFrame * 2));
    }

    // NRD's own textures never leave GENERAL. The application's are brought there, for compute work of any kind.
    userTextureUses_.clear();
    for (const auto &image : userTextures) {
        if (image != nullptr) {
            userTextureUses_.emplace_back(image, gpu::ImageUsage::General, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        }
    }
    commandBuffer.sync(userTextureUses_);

    const nrd::SPIRVBindingOffsets &offsets = nrd::GetLibraryDesc()->spirvBindingOffsets;
    const VkCommandBuffer vkCommandBuffer = commandBuffer.vkCommandBuffer();
    auto *constantData = static_cast<uint8_t *>(constants_.buffer->mappedPtr());
    const VkDeviceSize frameBase = VkDeviceSize(frameSlot % frameSlots_) * constants_.slotsPerFrame * constantSlotSize_;
    VkDeviceSize nextSlot = frameBase;
    uint32_t constantOffset = static_cast<uint32_t>(frameBase);
    bool constantsBound = false;

    for (uint32_t d = 0; d < dispatchCount; d++) {
        const nrd::DispatchDesc &dispatch = dispatches[d];
        const nrd::PipelineDesc &nrdPipeline = desc_->pipelines[dispatch.pipelineIndex];
        const Pipeline &pipeline = pipelines_[dispatch.pipelineIndex];

        if (bringsConstants(dispatch, constantsBound)) {
            std::memcpy(constantData + nextSlot, dispatch.constantBufferData, dispatch.constantBufferDataSize);
            constantOffset = static_cast<uint32_t>(nextSlot);
            nextSlot += constantSlotSize_;
            constantsBound = true;
        }

        // dispatch.resources follows the pipeline's ranges in order: the sampled textures, then the storage ones.
        writes_.clear();
        imageInfos_.resize(dispatch.resourcesNum);
        uint32_t resource = 0;
        for (uint32_t r = 0; r < nrdPipeline.resourceRangesNum; r++) {
            const nrd::ResourceRangeDesc &range = nrdPipeline.resourceRanges[r];
            const bool storage = range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
            const uint32_t first = (storage ? offsets.storageTextureAndBufferOffset : offsets.textureOffset) +
                                   desc_->resourcesBaseRegisterIndex;
            for (uint32_t i = 0; i < range.descriptorsNum; i++, resource++) {
                imageInfos_[resource] = {VK_NULL_HANDLE,
                                         texture(dispatch.resources[resource], userTextures)->vkImageView(),
                                         VK_IMAGE_LAYOUT_GENERAL};
                VkWriteDescriptorSet &write = writes_.emplace_back(VkWriteDescriptorSet{
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET});
                write.dstBinding = first + i;
                write.descriptorCount = 1;
                write.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                write.pImageInfo = &imageInfos_[resource];
            }
        }

        // Each dispatch reads what the ones before it wrote, this frame or an earlier one.
        commandBuffer.memoryBarrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
        vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout,
                                desc_->constantBufferAndSamplersSpaceIndex, 1, &constants_.set, 1, &constantOffset);
        vkCmdPushDescriptorSetKHR(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout,
                                  desc_->resourcesSpaceIndex, static_cast<uint32_t>(writes_.size()), writes_.data());
        vkCmdDispatch(vkCommandBuffer, dispatch.gridWidth, dispatch.gridHeight, 1);
    }

    if (nextSlot != frameBase) constants_.buffer->flush();
}

VkFormat NrdWrapper::normalRoughnessFormat() {
    switch (nrd::GetLibraryDesc()->normalEncoding) {
        case nrd::NormalEncoding::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case nrd::NormalEncoding::RGBA8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
        case nrd::NormalEncoding::R10_G10_B10_A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case nrd::NormalEncoding::RGBA16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
        case nrd::NormalEncoding::RGBA16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
        default: break;
    }
    throw std::runtime_error("NRD: the library uses a normal encoding this renderer has no format for");
}
