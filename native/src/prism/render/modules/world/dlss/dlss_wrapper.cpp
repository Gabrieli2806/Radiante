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

#include "prism/render/modules/world/dlss/dlss_wrapper.hpp"

#include <nvsdk_ngx_defs_dlssg.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_dlssd.h>
#include <nvsdk_ngx_helpers_dlssd_vk.h>
#include <nvsdk_ngx_helpers_vk.h>

#include <glm/gtc/type_ptr.hpp>

#include <cassert>
#include <ostream>
#include <source_location>
#include <utility>

#include "prism/gpu/command.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/image.hpp"
#include "prism/gpu/instance.hpp"
#include "prism/gpu/physical_device.hpp"
#include "prism/util/logging.hpp"

namespace {

// NGX needs an application ID. NVIDIA has not assigned one; any value but 0 is accepted.
constexpr unsigned long long kApplicationId = 0xbaadf00dbaadcafe;

// One GPU: features are created on, and visible to, the first node.
constexpr unsigned int kNodeMask = 0x1;

std::ostream &logError() {
    return radiante::out() << "[DLSS Error] ";
}

std::ostream &logWarning() {
    return radiante::out() << "[DLSS Warning] ";
}

std::ostream &logInfo() {
    return radiante::out() << "[DLSS Info] ";
}

// NGX describes results in wide text; the log is UTF-8.
std::string toUtf8(const wchar_t *text) {
    std::string out;
    if (text == nullptr) return out;
    for (; *text != L'\0'; ++text) {
        char32_t c = static_cast<char32_t>(*text);
        if constexpr (sizeof(wchar_t) == 2) {
            // UTF-16: a surrogate pair is one character.
            const char32_t next = static_cast<char32_t>(text[1]);
            if (c >= 0xD800 && c < 0xDC00 && next >= 0xDC00 && next < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (next - 0xDC00);
                ++text;
            }
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// NGX takes paths as wide strings. On Windows that is the path's own form; elsewhere each byte becomes one wide
// character, which keeps ASCII paths exact.
std::wstring ngxPath(const std::filesystem::path &path) {
#ifdef _WIN32
    return path.native();
#else
    const std::string &bytes = path.native();
    return std::wstring(bytes.begin(), bytes.end());
#endif
}

// Logs a failed NGX call under its name and the line it was made from.
NVSDK_NGX_Result check(NVSDK_NGX_Result result,
                       const char *call,
                       std::source_location where = std::source_location::current()) {
    return checkNgxResult(result, call, static_cast<int>(where.line()));
}

// Reads one capability parameter; a failed read is logged under the parameter's name.
template <typename T>
NVSDK_NGX_Result readParameter(const NVSDK_NGX_Parameter &params,
                               const char *name,
                               T &value,
                               std::source_location where = std::source_location::current()) {
    return check(params.Get(name, &value), name, where);
}

using OptimalSettingsQuery = NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter *,
                                                  unsigned int,
                                                  unsigned int,
                                                  NVSDK_NGX_PerfQuality_Value,
                                                  unsigned int *,
                                                  unsigned int *,
                                                  unsigned int *,
                                                  unsigned int *,
                                                  unsigned int *,
                                                  unsigned int *,
                                                  float *);

// Asks NGX's size table of a feature for the render sizes it supports at an output size and mode.
NVSDK_NGX_Result queryOptimalSizes(OptimalSettingsQuery query,
                                   const char *name,
                                   NVSDK_NGX_Parameter *params,
                                   const NgxContext::QuerySizeInfo &queryInfo,
                                   NgxContext::SupportedSizes &sizes) {
    if (params == nullptr) return NVSDK_NGX_Result_FAIL_NotInitialized;

    float sharpness = 0.0f; // asked for by the table, used by neither feature here
    const NVSDK_NGX_Result result =
        check(query(params, queryInfo.outputSize.width, queryInfo.outputSize.height, queryInfo.quality,
                    &sizes.optimalSize.width, &sizes.optimalSize.height, &sizes.maxSize.width, &sizes.maxSize.height,
                    &sizes.minSize.width, &sizes.minSize.height, &sharpness),
              name);
    if (NVSDK_NGX_FAILED(result)) return result;

    // A successful call can still hand back no size at all.
    if (sizes.optimalSize.width == 0 || sizes.optimalSize.height == 0) return NVSDK_NGX_Result_Fail;
    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_FeatureDiscoveryInfo discoveryInfo(NVSDK_NGX_Feature feature,
                                             const NVSDK_NGX_FeatureCommonInfo &commonInfo,
                                             const wchar_t *applicationDataPath) {
    NVSDK_NGX_FeatureDiscoveryInfo info = {};
    info.SDKVersion = NVSDK_NGX_Version_API;
    info.FeatureID = feature;
    info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Application_Id;
    info.Identifier.v.ApplicationId = kApplicationId;
    info.ApplicationDataPath = applicationDataPath;
    info.FeatureInfo = &commonInfo;
    return info;
}

NVSDK_NGX_Result appendInstanceExtensions(NVSDK_NGX_Feature feature, std::vector<VkExtensionProperties> &extensions) {
    const NVSDK_NGX_FeatureCommonInfo commonInfo = {};
    const NVSDK_NGX_FeatureDiscoveryInfo info = discoveryInfo(feature, commonInfo, nullptr);

    uint32_t count = 0;
    VkExtensionProperties *properties = nullptr;
    const NVSDK_NGX_Result result =
        check(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info, &count, &properties),
              "NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements");
    if (NVSDK_NGX_FAILED(result)) return result;

    if (properties != nullptr) extensions.insert(extensions.end(), properties, properties + count);
    return NVSDK_NGX_Result_Success;
}

// A failure is a warning here, not an error: the device is made without the feature.
NVSDK_NGX_Result appendDeviceExtensions(NVSDK_NGX_Feature feature,
                                        const std::shared_ptr<gpu::Instance> &instance,
                                        const std::shared_ptr<gpu::PhysicalDevice> &physicalDevice,
                                        std::vector<VkExtensionProperties> &extensions,
                                        const char *failureNote) {
    if (instance == nullptr || physicalDevice == nullptr) return NVSDK_NGX_Result_FAIL_InvalidParameter;

    const NVSDK_NGX_FeatureCommonInfo commonInfo = {};
    const NVSDK_NGX_FeatureDiscoveryInfo info = discoveryInfo(feature, commonInfo, L"");

    uint32_t count = 0;
    VkExtensionProperties *properties = nullptr;
    const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
        instance->vkInstance(), physicalDevice->vkPhysicalDevice(), &info, &count, &properties);
    if (NVSDK_NGX_FAILED(result)) {
        logWarning() << getNGXResultString(result) << failureNote << std::endl;
        return result;
    }

    if (properties != nullptr) extensions.insert(extensions.end(), properties, properties + count);
    return NVSDK_NGX_Result_Success;
}

// NGX records the creation of a feature into a command buffer, which goes to the main queue on its own. Nothing waits
// for it: it is submitted ahead of every frame that evaluates the feature, on the same queue.
template <typename Record>
NVSDK_NGX_Result createOnMainQueue(const std::shared_ptr<gpu::Device> &device,
                                   const std::shared_ptr<gpu::CommandPool> &cmdPool,
                                   Record &&record) {
    std::shared_ptr<gpu::CommandBuffer> cmdBuffer = gpu::CommandBuffer::create(device, cmdPool);
    cmdBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    const NVSDK_NGX_Result result = record(cmdBuffer->vkCommandBuffer());
    // A failed creation is not submitted: what NGX recorded of it is not known to be complete.
    if (NVSDK_NGX_FAILED(result)) return result;
    cmdBuffer->end()->submitMainQueueIndividual(device);
    return NVSDK_NGX_Result_Success;
}

} // namespace

std::string getNGXResultString(NVSDK_NGX_Result result) {
    return toUtf8(GetNGXResultAsString(result));
}

NVSDK_NGX_Result checkNgxResult(NVSDK_NGX_Result result, const char *func, int line) {
    if (NVSDK_NGX_FAILED(result)) {
        logError() << getNGXResultString(result) << " at " << (func != nullptr ? func : "?") << ":" << line
                   << std::endl;
    }
    return result;
}

// ---- NgxContext

NgxContext::~NgxContext() {
    assert(device_ == nullptr && ngxParams_ == nullptr && "NgxContext::deinit was not called");
}

NVSDK_NGX_Result NgxContext::init(const NgxInitInfo &initInfo) {
    if (!initInfo.instance || !initInfo.physicalDevice || !initInfo.device) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    assert(device_ == nullptr && ngxParams_ == nullptr && "NgxContext::init called twice");

    // The game runs as java.exe, so NGX would look for the feature libraries (nvngx_dlss.dll, nvngx_dlssd.dll) only
    // next to the JVM. The folder they are kept in is searched first.
    applicationPath_ = ngxPath(initInfo.applicationPath);
    searchPaths_.assign(1, applicationPath_);
    for (const std::filesystem::path &extra : initInfo.extraSearchPaths) {
        if (!extra.empty()) searchPaths_.push_back(ngxPath(extra));
    }
    searchPathList_.clear();
    for (const std::wstring &path : searchPaths_) searchPathList_.push_back(path.c_str());

    NVSDK_NGX_FeatureCommonInfo featureInfo = {};
    featureInfo.LoggingInfo.MinimumLoggingLevel = initInfo.loggingLevel;
    featureInfo.PathListInfo.Path = searchPathList_.data();
    featureInfo.PathListInfo.Length = static_cast<unsigned int>(searchPathList_.size());

    NVSDK_NGX_Result result =
        check(NVSDK_NGX_VULKAN_Init(kApplicationId, applicationPath_.c_str(), initInfo.instance->vkInstance(),
                                    initInfo.physicalDevice->vkPhysicalDevice(), initInfo.device->vkDevice(),
                                    vkGetInstanceProcAddr, vkGetDeviceProcAddr, &featureInfo),
              "NVSDK_NGX_VULKAN_Init");
    if (NVSDK_NGX_FAILED(result)) return result;
    device_ = initInfo.device;

    result = check(NVSDK_NGX_VULKAN_GetCapabilityParameters(&ngxParams_), "NVSDK_NGX_VULKAN_GetCapabilityParameters");
    if (NVSDK_NGX_FAILED(result)) {
        deinit();
        return result;
    }
    return NVSDK_NGX_Result_Success;
}

void NgxContext::deinit() {
    if (ngxParams_ != nullptr) NVSDK_NGX_VULKAN_DestroyParameters(ngxParams_);
    if (device_ != nullptr) NVSDK_NGX_VULKAN_Shutdown1(device_->vkDevice());
    ngxParams_ = nullptr;
    device_ = nullptr;
}

NVSDK_NGX_Result NgxContext::queryDlssRRAvailable() {
    if (ngxParams_ == nullptr) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const NVSDK_NGX_Parameter &params = *ngxParams_;

    // SuperSamplingDenoising is DLSS-D, Ray Reconstruction; SuperSampling alone is the plain upscaler.
    int needsUpdatedDriver = 0;
    unsigned int minDriverMajor = 0;
    unsigned int minDriverMinor = 0;
    const NVSDK_NGX_Result updateRead =
        readParameter(params, NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver, needsUpdatedDriver);
    const NVSDK_NGX_Result majorRead =
        readParameter(params, NVSDK_NGX_Parameter_SuperSamplingDenoising_MinDriverVersionMajor, minDriverMajor);
    const NVSDK_NGX_Result minorRead =
        readParameter(params, NVSDK_NGX_Parameter_SuperSamplingDenoising_MinDriverVersionMinor, minDriverMinor);
    if (NVSDK_NGX_SUCCEED(updateRead) && needsUpdatedDriver && NVSDK_NGX_SUCCEED(majorRead) &&
        NVSDK_NGX_SUCCEED(minorRead)) {
        logWarning() << "Minimum driver version required: " << minDriverMajor << "." << minDriverMinor << std::endl;
        return NVSDK_NGX_Result_FAIL_OutOfDate;
    }

    int available = 0;
    const NVSDK_NGX_Result availableRead =
        readParameter(params, NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, available);
    if (NVSDK_NGX_FAILED(availableRead) || !available) {
        logWarning() << "not available on this hardware/platform (query=" << static_cast<int>(availableRead)
                     << ", supported=" << available << ")" << std::endl;
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    }

    int initResult = 0;
    const NVSDK_NGX_Result initRead =
        readParameter(params, NVSDK_NGX_Parameter_SuperSamplingDenoising_FeatureInitResult, initResult);
    if (NVSDK_NGX_FAILED(initRead) || !initResult) {
        logWarning() << "denied for this application" << std::endl;
        return NVSDK_NGX_Result_FAIL_Denied;
    }
    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result NgxContext::queryDlssSRAvailable() {
    if (ngxParams_ == nullptr) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const NVSDK_NGX_Parameter &params = *ngxParams_;

    int needsUpdatedDriver = 0;
    unsigned int minDriverMajor = 0;
    unsigned int minDriverMinor = 0;
    const NVSDK_NGX_Result updateRead =
        readParameter(params, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, needsUpdatedDriver);
    const NVSDK_NGX_Result majorRead =
        readParameter(params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, minDriverMajor);
    const NVSDK_NGX_Result minorRead =
        readParameter(params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, minDriverMinor);
    if (NVSDK_NGX_SUCCEED(updateRead) && needsUpdatedDriver && NVSDK_NGX_SUCCEED(majorRead) &&
        NVSDK_NGX_SUCCEED(minorRead)) {
        logWarning() << "DLSS needs a newer driver, minimum " << minDriverMajor << "." << minDriverMinor << std::endl;
        return NVSDK_NGX_Result_FAIL_OutOfDate;
    }

    int available = 0;
    const NVSDK_NGX_Result availableRead =
        readParameter(params, NVSDK_NGX_Parameter_SuperSampling_Available, available);
    if (NVSDK_NGX_FAILED(availableRead) || !available) {
        logWarning() << "DLSS super resolution is not available on this hardware/platform" << std::endl;
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    }

    int initResult = 0;
    const NVSDK_NGX_Result initRead =
        readParameter(params, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, initResult);
    if (NVSDK_NGX_FAILED(initRead) || !initResult) {
        logWarning() << "DLSS super resolution is denied for this application" << std::endl;
        return NVSDK_NGX_Result_FAIL_Denied;
    }
    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result NgxContext::querySupportedDlssInputSizes(const QuerySizeInfo &queryInfo, SupportedSizes &sizes) {
    // Ray Reconstruction has no Ultra Quality mode.
    assert(queryInfo.quality != NVSDK_NGX_PerfQuality_Value_UltraQuality);
    return queryOptimalSizes(NGX_DLSSD_GET_OPTIMAL_SETTINGS, "NGX_DLSSD_GET_OPTIMAL_SETTINGS", ngxParams_, queryInfo,
                             sizes);
}

NVSDK_NGX_Result NgxContext::querySupportedDlssSRInputSizes(const QuerySizeInfo &queryInfo, SupportedSizes &sizes) {
    return queryOptimalSizes(NGX_DLSS_GET_OPTIMAL_SETTINGS, "NGX_DLSS_GET_OPTIMAL_SETTINGS", ngxParams_, queryInfo,
                             sizes);
}

NVSDK_NGX_Result NgxContext::initDlssRR(const DlssRRInitInfo &initInfo,
                                        std::shared_ptr<gpu::CommandPool> cmdPool,
                                        std::shared_ptr<DlssRR> dlssrr) {
    if (dlssrr == nullptr || cmdPool == nullptr) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    if (device_ == nullptr || ngxParams_ == nullptr) return NVSDK_NGX_Result_FAIL_NotInitialized;
    return dlssrr->init(device_, std::move(cmdPool), ngxParams_, initInfo);
}

NVSDK_NGX_Result NgxContext::initDlssSR(const DlssSRInitInfo &initInfo,
                                        std::shared_ptr<gpu::CommandPool> cmdPool,
                                        std::shared_ptr<DlssSR> dlsssr) {
    if (dlsssr == nullptr || cmdPool == nullptr) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    if (device_ == nullptr || ngxParams_ == nullptr) return NVSDK_NGX_Result_FAIL_NotInitialized;
    return dlsssr->init(device_, std::move(cmdPool), ngxParams_, initInfo);
}

NVSDK_NGX_Result NgxContext::queryDlssFrameGenerationAvailable() {
    if (ngxParams_ == nullptr) return NVSDK_NGX_Result_Fail;

    // Failed reads are not logged here: most GPUs have no frame generation.
    int needsUpdatedDriver = 0;
    const NVSDK_NGX_Result updateRead =
        ngxParams_->Get(NVSDK_NGX_Parameter_FrameGeneration_NeedsUpdatedDriver, &needsUpdatedDriver);
    if (NVSDK_NGX_SUCCEED(updateRead) && needsUpdatedDriver) {
        unsigned int minDriverMajor = 0;
        unsigned int minDriverMinor = 0;
        ngxParams_->Get(NVSDK_NGX_Parameter_FrameGeneration_MinDriverVersionMajor, &minDriverMajor);
        ngxParams_->Get(NVSDK_NGX_Parameter_FrameGeneration_MinDriverVersionMinor, &minDriverMinor);
        logWarning() << "DLSS frame generation needs a newer driver, minimum " << minDriverMajor << "."
                     << minDriverMinor << std::endl;
        return NVSDK_NGX_Result_FAIL_OutOfDate;
    }

    int available = 0;
    if (NVSDK_NGX_FAILED(ngxParams_->Get(NVSDK_NGX_Parameter_FrameGeneration_Available, &available)) || !available) {
        logInfo() << "DLSS frame generation is not available on this GPU" << std::endl;
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    }

    int initResult = 0;
    if (NVSDK_NGX_FAILED(ngxParams_->Get(NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult, &initResult)) ||
        !initResult) {
        logWarning() << "DLSS frame generation is denied for this application" << std::endl;
        return NVSDK_NGX_Result_FAIL_Denied;
    }
    return NVSDK_NGX_Result_Success;
}

uint32_t NgxContext::queryDlssFrameGenerationMaxFrames() {
    if (ngxParams_ == nullptr) return 1;
    unsigned int maxFrames = 0;
    const NVSDK_NGX_Result maxRead = ngxParams_->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &maxFrames);
    return NVSDK_NGX_FAILED(maxRead) || maxFrames == 0 ? 1u : maxFrames;
}

NVSDK_NGX_Parameter *NgxContext::parameters() {
    return ngxParams_;
}

NVSDK_NGX_Result
NgxContext::getDlssFrameGenerationRequiredInstanceExtensions(std::vector<VkExtensionProperties> &extensions) {
    return appendInstanceExtensions(NVSDK_NGX_Feature_FrameGeneration, extensions);
}

NVSDK_NGX_Result
NgxContext::getDlssFrameGenerationRequiredDeviceExtensions(std::shared_ptr<gpu::Instance> instance,
                                                           std::shared_ptr<gpu::PhysicalDevice> physicalDevice,
                                                           std::vector<VkExtensionProperties> &extensions) {
    return appendDeviceExtensions(NVSDK_NGX_Feature_FrameGeneration, instance, physicalDevice, extensions,
                                  " while querying DLSS frame generation device extensions");
}

NVSDK_NGX_Result NgxContext::getDlssRRRequiredInstanceExtensions(std::vector<VkExtensionProperties> &extensions) {
    return appendInstanceExtensions(NVSDK_NGX_Feature_RayReconstruction, extensions);
}

NVSDK_NGX_Result NgxContext::getDlssRRRequiredDeviceExtensions(std::shared_ptr<gpu::Instance> instance,
                                                               std::shared_ptr<gpu::PhysicalDevice> physicalDevice,
                                                               std::vector<VkExtensionProperties> &extensions) {
    return appendDeviceExtensions(NVSDK_NGX_Feature_RayReconstruction, instance, physicalDevice, extensions,
                                  " while querying DLSS device extensions; skipping.");
}

// ---- NgxFeature

NgxFeature::~NgxFeature() {
    // Not released here: that needs NGX alive and the GPU done with the feature, which only the owner knows.
    assert(handle_ == nullptr && "NgxFeature::deinit was not called");
}

void NgxFeature::deinit() {
    if (handle_ != nullptr) NVSDK_NGX_VULKAN_ReleaseFeature(handle_);
    handle_ = nullptr;
    params_ = nullptr;
    device_ = nullptr;
}

NVSDK_NGX_Resource_VK NgxFeature::resourceFor(gpu::DeviceLocalImage &image, bool output) const {
    const VkExtent2D size = output ? outputSize_ : inputSize_;
    return NVSDK_NGX_Create_ImageView_Resource_VK(image.vkImageView(), image.vkImage(), gpu::wholeColorSubresourceRange,
                                                  image.vkFormat(), size.width, size.height, output);
}

// ---- DlssRR

NVSDK_NGX_Result DlssRR::init(std::shared_ptr<gpu::Device> device,
                              std::shared_ptr<gpu::CommandPool> cmdPool,
                              NVSDK_NGX_Parameter *ngxParams,
                              const NgxContext::DlssRRInitInfo &info) {
    assert(!ready() && "DlssRR::init called twice");

    device_ = std::move(device);
    params_ = ngxParams;
    inputSize_ = info.inputSize;
    outputSize_ = info.outputSize;
    resources_.fill({});

    NVSDK_NGX_DLSSD_Create_Params create = {};
    create.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
    // The roughness travels in the w of the half-float normals, and the depth given is linear.
    create.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Packed;
    create.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_Linear;
    create.InWidth = inputSize_.width;
    create.InHeight = inputSize_.height;
    create.InTargetWidth = outputSize_.width;
    create.InTargetHeight = outputSize_.height;
    // Optional in the API but required here: the colour is HDR and the motion vectors are at render resolution.
    create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    create.InPerfQualityValue = info.quality;

    // The model each mode runs. DLAA is given none and runs NGX's default.
    for (const char *mode : {NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,
                             NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality,
                             NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced,
                             NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,
                             NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance}) {
        params_->Set(mode, static_cast<int>(info.preset));
    }

    return createOnMainQueue(device_, cmdPool, [&](VkCommandBuffer cmd) {
        return check(NGX_VULKAN_CREATE_DLSSD_EXT1(device_->vkDevice(), cmd, kNodeMask, kNodeMask, &handle_, params_,
                                                  &create),
                     "NGX_VULKAN_CREATE_DLSSD_EXT1");
    });
}

void DlssRR::setResource(DlssResource resourceId, std::shared_ptr<gpu::DeviceLocalImage> image) {
    assert(ready());
    resources_[resourceId] =
        image == nullptr ? NVSDK_NGX_Resource_VK{} : resourceFor(*image, resourceId == RESOURCE_COLOR_OUT);
}

void DlssRR::resetResource(DlssResource resourceId) {
    resources_[resourceId] = {};
}

NVSDK_NGX_Resource_VK *DlssRR::bound(DlssResource resourceId) {
    NVSDK_NGX_Resource_VK &resource = resources_[resourceId];
    return resource.Resource.ImageViewInfo.ImageView != VK_NULL_HANDLE ? &resource : nullptr;
}

NVSDK_NGX_Result DlssRR::denoise(std::shared_ptr<gpu::CommandBuffer> cmdBuffer,
                                 glm::uvec2 renderSize,
                                 glm::vec2 jitter,
                                 const glm::mat4 &modelView,
                                 const glm::mat4 &projection,
                                 bool reset) {
    if (!ready() || cmdBuffer == nullptr) return NVSDK_NGX_Result_FAIL_NotInitialized;

    NVSDK_NGX_VK_DLSSD_Eval_Params eval = {};
    eval.pInColor = bound(RESOURCE_COLOR_IN);
    eval.pInOutput = bound(RESOURCE_COLOR_OUT);
    eval.pInDiffuseAlbedo = bound(RESOURCE_DIFFUSE_ALBEDO);
    eval.pInSpecularAlbedo = bound(RESOURCE_SPECULAR_ALBEDO);
    eval.pInSpecularHitDistance = bound(RESOURCE_SPECULAR_HITDISTANCE);
    // The roughness is packed into the normals, so both read the same image.
    eval.pInNormals = bound(RESOURCE_NORMALROUGHNESS);
    eval.pInRoughness = bound(RESOURCE_NORMALROUGHNESS);
    eval.pInDepth = bound(RESOURCE_LINEARDEPTH);
    eval.pInMotionVectors = bound(RESOURCE_MOTIONVECTOR);

    // NGX counts the jitter with the opposite sign.
    eval.InJitterOffsetX = -jitter.x;
    eval.InJitterOffsetY = -jitter.y;
    eval.InMVScaleX = 1.0f;
    eval.InMVScaleY = 1.0f;
    eval.InRenderSubrectDimensions.Width = renderSize.x;
    eval.InRenderSubrectDimensions.Height = renderSize.y;

    // NGX reads row-major matrices that multiply from the left; glm's are column-major and multiply from the right.
    // The two transposes cancel out, so glm's matrices go in as they are.
    eval.pInWorldToViewMatrix = const_cast<float *>(glm::value_ptr(modelView));
    eval.pInViewToClipMatrix = const_cast<float *>(glm::value_ptr(projection));
    eval.InReset = reset;

    const NVSDK_NGX_Result result =
        check(NGX_VULKAN_EVALUATE_DLSSD_EXT(cmdBuffer->vkCommandBuffer(), handle_, params_, &eval),
              "NGX_VULKAN_EVALUATE_DLSSD_EXT");
    return NVSDK_NGX_FAILED(result) ? result : NVSDK_NGX_Result_Success;
}

// ---- DlssSR

NVSDK_NGX_Result DlssSR::init(std::shared_ptr<gpu::Device> device,
                              std::shared_ptr<gpu::CommandPool> cmdPool,
                              NVSDK_NGX_Parameter *ngxParams,
                              const NgxContext::DlssSRInitInfo &info) {
    assert(!ready() && "DlssSR::init called twice");

    device_ = std::move(device);
    params_ = ngxParams;
    inputSize_ = info.inputSize;
    outputSize_ = info.outputSize;
    resources_.fill({});

    NVSDK_NGX_DLSS_Create_Params create = {};
    create.Feature.InWidth = inputSize_.width;
    create.Feature.InHeight = inputSize_.height;
    create.Feature.InTargetWidth = outputSize_.width;
    create.Feature.InTargetHeight = outputSize_.height;
    create.Feature.InPerfQualityValue = info.quality;
    // HDR colour and render-resolution motion vectors, as for Ray Reconstruction.
    create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                                  NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;

    return createOnMainQueue(device_, cmdPool, [&](VkCommandBuffer cmd) {
        return check(NGX_VULKAN_CREATE_DLSS_EXT1(device_->vkDevice(), cmd, kNodeMask, kNodeMask, &handle_, params_,
                                                 &create),
                     "NGX_VULKAN_CREATE_DLSS_EXT1");
    });
}

void DlssSR::setResource(DlssResource resourceId, std::shared_ptr<gpu::DeviceLocalImage> image) {
    assert(ready());
    resources_[resourceId] =
        image == nullptr ? NVSDK_NGX_Resource_VK{} : resourceFor(*image, resourceId == RESOURCE_COLOR_OUT);
}

void DlssSR::resetResource(DlssResource resourceId) {
    resources_[resourceId] = {};
}

NVSDK_NGX_Resource_VK *DlssSR::bound(DlssResource resourceId) {
    NVSDK_NGX_Resource_VK &resource = resources_[resourceId];
    return resource.Resource.ImageViewInfo.ImageView != VK_NULL_HANDLE ? &resource : nullptr;
}

NVSDK_NGX_Result
DlssSR::upscale(std::shared_ptr<gpu::CommandBuffer> cmdBuffer, glm::uvec2 renderSize, glm::vec2 jitter, bool reset) {
    if (!ready() || cmdBuffer == nullptr) return NVSDK_NGX_Result_FAIL_NotInitialized;

    NVSDK_NGX_VK_DLSS_Eval_Params eval = {};
    eval.Feature.pInColor = bound(RESOURCE_COLOR_IN);
    eval.Feature.pInOutput = bound(RESOURCE_COLOR_OUT);
    eval.Feature.InSharpness = 0.0f;
    eval.pInDepth = bound(RESOURCE_DEPTH);
    eval.pInMotionVectors = bound(RESOURCE_MOTIONVECTOR);

    eval.InJitterOffsetX = -jitter.x;
    eval.InJitterOffsetY = -jitter.y;
    eval.InMVScaleX = 1.0f;
    eval.InMVScaleY = 1.0f;
    eval.InRenderSubrectDimensions.Width = renderSize.x;
    eval.InRenderSubrectDimensions.Height = renderSize.y;
    eval.InReset = reset;

    const NVSDK_NGX_Result result =
        check(NGX_VULKAN_EVALUATE_DLSS_EXT(cmdBuffer->vkCommandBuffer(), handle_, params_, &eval),
              "NGX_VULKAN_EVALUATE_DLSS_EXT");
    return NVSDK_NGX_FAILED(result) ? result : NVSDK_NGX_Result_Success;
}
