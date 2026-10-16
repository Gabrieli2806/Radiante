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

#pragma once

#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <NRD.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class Framework;

// The application's images NRD reads (IN_*) and writes (OUT_*), one slot per nrd::ResourceType. Only the slots the
// running denoiser uses need an image.
using NrdUserTextures = std::array<std::shared_ptr<gpu::DeviceLocalImage>, size_t(nrd::ResourceType::MAX_NUM)>;

// One NRD instance on the engine's device: the textures NRD keeps for itself (history and scratch, cleared once), the
// compute pipelines built from the SPIR-V the library ships, and the recording of a denoiser's dispatches. It is made
// for one resolution; a new size needs a new wrapper. Its Vulkan objects outlive it until no frame in flight uses them.
class NrdWrapper : public SharedObject<NrdWrapper> {
  public:
    NrdWrapper() = default;
    ~NrdWrapper();
    NrdWrapper(const NrdWrapper &) = delete;
    NrdWrapper &operator=(const NrdWrapper &) = delete;

    // `denoisers` are the ones this instance can run; each is identified by its own nrd::Denoiser value.
    void init(std::shared_ptr<Framework> framework,
              uint16_t width,
              uint16_t height,
              std::span<const nrd::Denoiser> denoisers);

    // Once per frame, before denoise().
    void setCommonSettings(const nrd::CommonSettings &settings);
    void setReblurSettings(nrd::Denoiser denoiser, const nrd::ReblurSettings &settings);
    void setRelaxSettings(nrd::Denoiser denoiser, const nrd::RelaxSettings &settings);

    // Records `denoiser` into the frame being recorded in slot `frameSlot`, once per frame. It reads the IN_* user
    // textures and writes the OUT_* ones; all of them are left in GENERAL, as compute work that may have written them.
    void denoise(gpu::CommandBuffer &commandBuffer,
                 uint32_t frameSlot,
                 nrd::Denoiser denoiser,
                 const NrdUserTextures &userTextures);

    // The format IN_NORMAL_ROUGHNESS must have: the library is built for one normal and roughness encoding.
    static VkFormat normalRoughnessFormat();

  private:
    struct InstanceDeleter {
        void operator()(nrd::Instance *instance) const;
    };

    // One of NRD's pipelines. Its textures are pushed (set NRD_RESOURCES_SPACE_INDEX); its samplers and constants come
    // from the shared set (NRD_CONSTANT_BUFFER_AND_SAMPLERS_SPACE_INDEX).
    struct Pipeline {
        VkDescriptorSetLayout textureLayout = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    // The constants of each dispatch, written by the CPU while recording into a slot of their own and bound by dynamic
    // offset. Every frame slot has its own region, so a frame never overwrites what a frame still in flight reads.
    struct ConstantRing {
        std::shared_ptr<gpu::HostVisibleBuffer> buffer;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE; // the shared set: samplers and constants
        uint32_t slotsPerFrame = 0;
    };

    void createTextures(uint16_t width, uint16_t height);
    void clearTextures(const std::shared_ptr<Framework> &framework);
    void createSharedLayout();
    void createPipelines();
    void createConstantRing(uint32_t slotsPerFrame);
    void retireConstantRing();
    void setDenoiserSettings(nrd::Denoiser denoiser, const void *settings);
    const std::shared_ptr<gpu::DeviceLocalImage> &texture(const nrd::ResourceDesc &resource,
                                                          const NrdUserTextures &userTextures) const;

    std::shared_ptr<gpu::Device> device_;
    std::shared_ptr<gpu::VMA> vma_;
    std::unique_ptr<nrd::Instance, InstanceDeleter> instance_;
    const nrd::InstanceDesc *desc_ = nullptr; // owned by instance_
    uint32_t frameSlots_ = 1;
    VkDeviceSize constantSlotSize_ = 0;
    bool commonSettingsRejected_ = false;

    std::vector<std::shared_ptr<gpu::DeviceLocalImage>> permanentTextures_;
    std::vector<std::shared_ptr<gpu::DeviceLocalImage>> transientTextures_;
    std::vector<std::shared_ptr<gpu::Sampler>> samplers_;
    VkDescriptorSetLayout sharedLayout_ = VK_NULL_HANDLE;
    std::vector<Pipeline> pipelines_;
    ConstantRing constants_;

    // Reused by every dispatch, so recording allocates nothing once warm.
    std::vector<VkWriteDescriptorSet> writes_;
    std::vector<VkDescriptorImageInfo> imageInfos_;
    std::vector<gpu::ImageUse> userTextureUses_;
};
