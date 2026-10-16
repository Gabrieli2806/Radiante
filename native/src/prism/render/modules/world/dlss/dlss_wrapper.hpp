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

#include "nvsdk_ngx_vk.h"
#include "nvsdk_ngx_defs_dlssd.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gpu {
class Instance;
class Device;
class PhysicalDevice;
class CommandPool;
class CommandBuffer;
class DeviceLocalImage;
} // namespace gpu

// The name NGX gives a result, for the log.
std::string getNGXResultString(NVSDK_NGX_Result result);

// Logs a failed result with where it came from (`func`, `line`) and returns it unchanged.
NVSDK_NGX_Result checkNgxResult(NVSDK_NGX_Result result, const char *func, int line);

class DlssRR;
class DlssSR;

/**
 * NGX on the renderer's Vulkan device. There is one per process: it says what the GPU and driver support, creates
 * the DLSS features, and lends its parameter block to frame generation.
 */
class NgxContext : public SharedObject<NgxContext> {
  public:
    struct NgxInitInfo {
        std::shared_ptr<gpu::Instance> instance;
        std::shared_ptr<gpu::PhysicalDevice> physicalDevice;
        std::shared_ptr<gpu::Device> device;
#ifdef NDEBUG
        NVSDK_NGX_Logging_Level loggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;
#else
        NVSDK_NGX_Logging_Level loggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
#endif
        // Where NGX keeps its logs and temporary files; also the first folder it searches for the feature libraries.
        // Kept as a path so a game folder with non-ASCII characters reaches NGX intact.
        std::filesystem::path applicationPath;
        // More folders to search for the feature libraries, after applicationPath.
        std::vector<std::filesystem::path> extraSearchPaths;
    };

    struct SupportedSizes {
        VkExtent2D minSize = {};
        VkExtent2D maxSize = {};
        VkExtent2D optimalSize = {};
    };

    struct QuerySizeInfo {
        VkExtent2D outputSize = {};
        NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
    };

    struct DlssRRInitInfo {
        VkExtent2D inputSize = {};  // the noisy images the world is traced into
        VkExtent2D outputSize = {}; // the denoised, upscaled result
        NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
        // E is the latest transformer model.
        NVSDK_NGX_RayReconstruction_Hint_Render_Preset preset = NVSDK_NGX_RayReconstruction_Hint_Render_Preset_E;
    };

    struct DlssSRInitInfo {
        VkExtent2D inputSize = {};  // what the world is rendered at
        VkExtent2D outputSize = {}; // what it is upscaled to
        NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
    };

    NgxContext() = default;
    // Does not shut NGX down: deinit() must have done it. The last owner may let go at process exit, when calling
    // into NGX or the driver is no longer safe.
    ~NgxContext();

    NgxContext(const NgxContext &) = delete;
    NgxContext &operator=(const NgxContext &) = delete;

    NVSDK_NGX_Result init(const NgxInitInfo &initInfo);
    // Shuts NGX down. Every feature created through this context must have been released before.
    void deinit();

    // Whether Ray Reconstruction (DLSS-D, the denoiser) can be created.
    NVSDK_NGX_Result queryDlssRRAvailable();
    // Whether plain DLSS (super resolution only) can be created. A card or driver may offer one without the other.
    NVSDK_NGX_Result queryDlssSRAvailable();

    // The render sizes Ray Reconstruction supports for an output size and mode.
    NVSDK_NGX_Result querySupportedDlssInputSizes(const QuerySizeInfo &queryInfo, SupportedSizes &sizes);
    // The same for plain DLSS, which has its own table of sizes.
    NVSDK_NGX_Result querySupportedDlssSRInputSizes(const QuerySizeInfo &queryInfo, SupportedSizes &sizes);

    // Create the feature inside `dlssrr` / `dlsssr`; the creation commands go to the main queue from `cmdPool`.
    NVSDK_NGX_Result initDlssRR(const DlssRRInitInfo &initInfo,
                                std::shared_ptr<gpu::CommandPool> cmdPool,
                                std::shared_ptr<DlssRR> dlssrr);
    NVSDK_NGX_Result initDlssSR(const DlssSRInitInfo &initInfo,
                                std::shared_ptr<gpu::CommandPool> cmdPool,
                                std::shared_ptr<DlssSR> dlsssr);

    // DLSS Frame Generation, driven through NGX directly.
    NVSDK_NGX_Result queryDlssFrameGenerationAvailable();
    // How many frames DLSS may generate per rendered one; 1 when NGX does not say.
    uint32_t queryDlssFrameGenerationMaxFrames();
    // The capability parameter block every feature is created and evaluated with; null before init and after deinit.
    NVSDK_NGX_Parameter *parameters();

    // Append the Vulkan extensions a feature needs. The instance ones are known before the instance exists, the
    // device ones before the device does.
    static NVSDK_NGX_Result
    getDlssFrameGenerationRequiredInstanceExtensions(std::vector<VkExtensionProperties> &extensions);
    static NVSDK_NGX_Result
    getDlssFrameGenerationRequiredDeviceExtensions(std::shared_ptr<gpu::Instance> instance,
                                                   std::shared_ptr<gpu::PhysicalDevice> physicalDevice,
                                                   std::vector<VkExtensionProperties> &extensions);
    static NVSDK_NGX_Result getDlssRRRequiredInstanceExtensions(std::vector<VkExtensionProperties> &extensions);
    static NVSDK_NGX_Result getDlssRRRequiredDeviceExtensions(std::shared_ptr<gpu::Instance> instance,
                                                              std::shared_ptr<gpu::PhysicalDevice> physicalDevice,
                                                              std::vector<VkExtensionProperties> &extensions);

  private:
    std::shared_ptr<gpu::Device> device_;
    NVSDK_NGX_Parameter *ngxParams_ = nullptr;
    std::wstring applicationPath_;
    // The feature library search list handed to NGX, and the strings it points into.
    std::vector<std::wstring> searchPaths_;
    std::vector<const wchar_t *> searchPathList_;
};

/**
 * What DlssRR and DlssSR share: the NGX feature, the parameter block it was created with and the sizes it works at.
 * The owner releases the feature with deinit() once no submitted frame can still evaluate it.
 */
class NgxFeature {
  public:
    NgxFeature(const NgxFeature &) = delete;
    NgxFeature &operator=(const NgxFeature &) = delete;

    // Created and not released.
    bool ready() const {
        return handle_ != nullptr;
    }

    void deinit();

  protected:
    NgxFeature() = default;
    ~NgxFeature();

    // NGX's view of an image: the output is read and written at the output size, everything else is read at the
    // render size.
    NVSDK_NGX_Resource_VK resourceFor(gpu::DeviceLocalImage &image, bool output) const;

    std::shared_ptr<gpu::Device> device_;
    NVSDK_NGX_Parameter *params_ = nullptr;
    NVSDK_NGX_Handle *handle_ = nullptr;
    VkExtent2D inputSize_ = {};
    VkExtent2D outputSize_ = {};
};

/**
 * Ray Reconstruction (DLSS-D): denoises and upscales the traced picture in one step, guided by the G-buffer. It takes
 * the place of the denoiser rather than following it.
 */
class DlssRR : public NgxFeature, public SharedObject<DlssRR> {
  public:
    DlssRR() = default;
    ~DlssRR() = default;

    enum DlssResource {
        // Required
        RESOURCE_COLOR_IN = 0,
        RESOURCE_COLOR_OUT,
        RESOURCE_DIFFUSE_ALBEDO,
        RESOURCE_SPECULAR_ALBEDO,
        RESOURCE_NORMALROUGHNESS,
        RESOURCE_MOTIONVECTOR,
        RESOURCE_LINEARDEPTH,
        // Optional guides
        RESOURCE_SPECULAR_HITDISTANCE,

        RESOURCE_NUM
    };

    // Binds an image to an input or the output; a null image unbinds it.
    void setResource(DlssResource resourceId, std::shared_ptr<gpu::DeviceLocalImage> image);
    void resetResource(DlssResource resourceId);

    // Records the denoising into `cmdBuffer`. `renderSize` is the part of the inputs (from [0,0]) that was rendered,
    // `jitter` this frame's subpixel offset in [-0.5, 0.5], `modelView` and `projection` the camera. `reset` drops the
    // history, for when the picture changes completely.
    NVSDK_NGX_Result denoise(std::shared_ptr<gpu::CommandBuffer> cmdBuffer,
                             glm::uvec2 renderSize,
                             glm::vec2 jitter,
                             const glm::mat4 &modelView,
                             const glm::mat4 &projection,
                             bool reset = false);

  private:
    friend class NgxContext;
    NVSDK_NGX_Result init(std::shared_ptr<gpu::Device> device,
                          std::shared_ptr<gpu::CommandPool> cmdPool,
                          NVSDK_NGX_Parameter *ngxParams,
                          const NgxContext::DlssRRInitInfo &info);

    NVSDK_NGX_Resource_VK *bound(DlssResource resourceId);

    std::array<NVSDK_NGX_Resource_VK, RESOURCE_NUM> resources_ = {};
};

/**
 * Plain DLSS: super resolution only. It enlarges a picture that has already been denoised, so it needs nothing but
 * colour, depth and motion vectors.
 */
class DlssSR : public NgxFeature, public SharedObject<DlssSR> {
  public:
    DlssSR() = default;
    ~DlssSR() = default;

    enum DlssResource {
        RESOURCE_COLOR_IN = 0,
        RESOURCE_COLOR_OUT,
        RESOURCE_MOTIONVECTOR,
        RESOURCE_DEPTH,

        RESOURCE_NUM
    };

    // Binds an image to an input or the output; a null image unbinds it.
    void setResource(DlssResource resourceId, std::shared_ptr<gpu::DeviceLocalImage> image);
    void resetResource(DlssResource resourceId);

    // Records the upscaling into `cmdBuffer`; the arguments mean what they mean for DlssRR::denoise.
    NVSDK_NGX_Result
    upscale(std::shared_ptr<gpu::CommandBuffer> cmdBuffer, glm::uvec2 renderSize, glm::vec2 jitter, bool reset = false);

  private:
    friend class NgxContext;
    NVSDK_NGX_Result init(std::shared_ptr<gpu::Device> device,
                          std::shared_ptr<gpu::CommandPool> cmdPool,
                          NVSDK_NGX_Parameter *ngxParams,
                          const NgxContext::DlssSRInitInfo &info);

    NVSDK_NGX_Resource_VK *bound(DlssResource resourceId);

    std::array<NVSDK_NGX_Resource_VK, RESOURCE_NUM> resources_ = {};
};
