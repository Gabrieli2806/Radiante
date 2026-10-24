#include "prism/gpu/device.hpp"

#include "prism/gpu/diagnostics.hpp"
#include "prism/gpu/instance.hpp"
#include "prism/gpu/physical_device.hpp"
#include "prism/gpu/queue_lock.hpp"
#include "prism/render/framegen/reflex.hpp"
#include "prism/render/modules/world/dlss/dlss_wrapper.hpp"
#include "prism/render/modules/world/xess_upscaler/xess_wrapper.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gpu {
namespace {

std::ostream &deviceLog() {
    return radiante::out() << "[Device] ";
}

std::ostream &deviceError() {
    return radiante::err() << "[Device] ";
}

// What createMerged enabled on the device it made last. A wrapper answers for its own device only.
struct CreatedDevice {
    VkDevice handle = VK_NULL_HANDLE;
    bool dlss = false;
    bool dlssFrameGeneration = false;
    bool fsrFrameGeneration = false;
    bool shaderExecutionReordering = false;
    bool xess = false;
};

CreatedDevice g_created;

// ---- extensions

// Requested on every device: ray tracing, and what the render modules, upscalers and frame generation build on.
constexpr const char *kRendererExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_SPIRV_1_4_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
    VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME,
};

// What FSR frame generation needs from the device.
constexpr const char *kFsrFrameGenerationExtensions[] = {
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
    VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
};

// Vulkan 1.2's bufferDeviceAddress feature, which the renderer enables, does what this extension does: an SDK that
// lists it is served without it.
constexpr std::string_view kBufferDeviceAddressExtension = VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;

// The device extensions an SDK (DLSS, DLSS frame generation, XeSS) asked for. `known` is false when it could not tell;
// that feature is then left off.
struct Requirement {
    bool known = false;
    std::vector<std::string> extensions;
};

template <typename Query>
Requirement ngxRequirement(Query &&query) {
    std::vector<VkExtensionProperties> extensions;
    Requirement requirement;
    requirement.known = NVSDK_NGX_SUCCEED(query(extensions));
    if (requirement.known) {
        for (const VkExtensionProperties &extension : extensions) {
            requirement.extensions.emplace_back(extension.extensionName);
        }
    }
    return requirement;
}

// The extensions the device is made with: Minecraft's own, then the renderer's, each once and in the order first asked
// for. Those the GPU does not offer are reported and left out.
class DeviceExtensions {
  public:
    explicit DeviceExtensions(VkPhysicalDevice physicalDevice)
        : offered_(PhysicalDevice::extensionNames(physicalDevice)) {}

    bool offered(const std::string &name) const {
        return offered_.contains(name);
    }

    // Whether an SDK could tell what it needs and the GPU offers all of it.
    bool serves(const Requirement &requirement) const {
        return requirement.known && std::ranges::all_of(requirement.extensions, [&](const std::string &name) {
                   return name == kBufferDeviceAddressExtension || offered(name);
               });
    }

    void request(const std::string &name) {
        if (requestedNames_.insert(name).second) requested_.push_back(name);
    }

    // An NGX feature's extensions, but for the one the bufferDeviceAddress feature stands in for (a device may not be
    // given both).
    void requestNgx(const Requirement &requirement) {
        for (const std::string &name : requirement.extensions) {
            if (name != kBufferDeviceAddressExtension) request(name);
        }
    }

    // Settles the list. The names point into this object: nothing is requested after.
    std::vector<const char *> enable() {
        std::vector<const char *> names;
        names.reserve(requested_.size());
        for (const std::string &name : requested_) {
            if (!offered(name)) {
                deviceError() << "extension not supported, skipping: " << name << std::endl;
                continue;
            }
            enabled_.insert(name);
            names.push_back(name.c_str());
        }
        return names;
    }

    bool enabled(const std::string &name) const {
        return enabled_.contains(name);
    }

  private:
    std::unordered_set<std::string> offered_;
    std::vector<std::string> requested_;
    std::unordered_set<std::string> requestedNames_;
    std::unordered_set<std::string> enabled_;
};

// ---- features

// The feature structures the renderer fills, chained in a fixed order. The driver fills one with what the GPU supports;
// createMerged fills another with what the device enables. The chain points into the object, so it stays where it is.
struct FeatureChain {
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV invocationReorder{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceVulkan12Features vulkan12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features vulkan11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan13Features vulkan13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT dynamicState2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT};
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamicState3{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT};
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vertexInputDynamicState{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT};
    VkPhysicalDeviceCoherentMemoryFeaturesAMD coherentMemory{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD};
    VkPhysicalDeviceMaintenance5Features maintenance5{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES};
    // The end of the chain, where structures copied from Minecraft's request are added.
    VkBaseOutStructure *tail = nullptr;

    // The invocation reorder structure is only chained where the GPU has the extension.
    explicit FeatureChain(bool withInvocationReorder) {
        features2.pNext = withInvocationReorder ? static_cast<void *>(&invocationReorder) : &rayTracingPipeline;
        invocationReorder.pNext = &rayTracingPipeline;
        rayTracingPipeline.pNext = &accelerationStructure;
        accelerationStructure.pNext = &vulkan12;
        vulkan12.pNext = &vulkan11;
        vulkan11.pNext = &vulkan13;
        vulkan13.pNext = &dynamicState2;
        dynamicState2.pNext = &dynamicState3;
        dynamicState3.pNext = &vertexInputDynamicState;
        vertexInputDynamicState.pNext = &coherentMemory;
        coherentMemory.pNext = &maintenance5;
        maintenance5.pNext = nullptr;
        tail = reinterpret_cast<VkBaseOutStructure *>(&maintenance5);
    }

    FeatureChain(const FeatureChain &) = delete;
    FeatureChain &operator=(const FeatureChain &) = delete;

    void append(void *structure) {
        auto *node = static_cast<VkBaseOutStructure *>(structure);
        node->pNext = nullptr;
        tail->pNext = node;
        tail = node;
    }
};

// Takes each listed feature over from what the GPU supports.
template <typename T, typename... Fields>
void enableSupported(T &enabled, const T &supported, Fields... fields) {
    ((enabled.*fields = supported.*fields), ...);
}

// The features the renderer uses, wherever the GPU has them. An extension's features only where it is enabled.
void enableRendererFeatures(FeatureChain &enabled, const FeatureChain &supported, const DeviceExtensions &extensions) {
    using Core = VkPhysicalDeviceFeatures;
    enableSupported(enabled.features2.features, supported.features2.features, &Core::independentBlend,
                    &Core::shaderClipDistance, &Core::shaderCullDistance, &Core::logicOp, &Core::fillModeNonSolid,
                    &Core::depthBiasClamp, &Core::shaderInt64, &Core::shaderFloat64, &Core::shaderInt16,
                    &Core::shaderStorageImageReadWithoutFormat, &Core::shaderStorageImageWriteWithoutFormat);

    using Vulkan11 = VkPhysicalDeviceVulkan11Features;
    enableSupported(enabled.vulkan11, supported.vulkan11, &Vulkan11::storageBuffer16BitAccess);

    using Vulkan12 = VkPhysicalDeviceVulkan12Features;
    enableSupported(
        enabled.vulkan12, supported.vulkan12, &Vulkan12::bufferDeviceAddress,
        &Vulkan12::descriptorBindingUpdateUnusedWhilePending, &Vulkan12::descriptorBindingPartiallyBound,
        &Vulkan12::descriptorIndexing, &Vulkan12::runtimeDescriptorArray,
        &Vulkan12::descriptorBindingVariableDescriptorCount, &Vulkan12::shaderSampledImageArrayNonUniformIndexing,
        &Vulkan12::descriptorBindingUniformBufferUpdateAfterBind,
        &Vulkan12::descriptorBindingSampledImageUpdateAfterBind,
        &Vulkan12::descriptorBindingStorageImageUpdateAfterBind,
        &Vulkan12::descriptorBindingStorageBufferUpdateAfterBind, &Vulkan12::shaderFloat16,
        &Vulkan12::shaderBufferInt64Atomics, &Vulkan12::shaderStorageBufferArrayNonUniformIndexing,
        &Vulkan12::shaderStorageImageArrayNonUniformIndexing, &Vulkan12::shaderUniformBufferArrayNonUniformIndexing);

    using Vulkan13 = VkPhysicalDeviceVulkan13Features;
    enableSupported(enabled.vulkan13, supported.vulkan13, &Vulkan13::shaderDemoteToHelperInvocation,
                    &Vulkan13::synchronization2);

    using AccelerationStructure = VkPhysicalDeviceAccelerationStructureFeaturesKHR;
    enableSupported(enabled.accelerationStructure, supported.accelerationStructure,
                    &AccelerationStructure::accelerationStructure,
                    &AccelerationStructure::descriptorBindingAccelerationStructureUpdateAfterBind);
    enableSupported(enabled.rayTracingPipeline, supported.rayTracingPipeline,
                    &VkPhysicalDeviceRayTracingPipelineFeaturesKHR::rayTracingPipeline);

    if (extensions.enabled(VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME)) {
        using DynamicState2 = VkPhysicalDeviceExtendedDynamicState2FeaturesEXT;
        enableSupported(enabled.dynamicState2, supported.dynamicState2, &DynamicState2::extendedDynamicState2,
                        &DynamicState2::extendedDynamicState2LogicOp);
    }
    if (extensions.enabled(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME)) {
        using DynamicState3 = VkPhysicalDeviceExtendedDynamicState3FeaturesEXT;
        enableSupported(
            enabled.dynamicState3, supported.dynamicState3, &DynamicState3::extendedDynamicState3PolygonMode,
            &DynamicState3::extendedDynamicState3ColorBlendEnable,
            &DynamicState3::extendedDynamicState3ColorBlendEquation,
            &DynamicState3::extendedDynamicState3ColorWriteMask, &DynamicState3::extendedDynamicState3LogicOpEnable);
    }
    if (extensions.enabled(VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME)) {
        enableSupported(enabled.vertexInputDynamicState, supported.vertexInputDynamicState,
                        &VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT::vertexInputDynamicState);
    }
    if (extensions.enabled(VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME)) {
        enableSupported(enabled.coherentMemory, supported.coherentMemory,
                        &VkPhysicalDeviceCoherentMemoryFeaturesAMD::deviceCoherentMemory);
    }
    if (extensions.enabled(VK_KHR_MAINTENANCE_5_EXTENSION_NAME)) {
        enableSupported(enabled.maintenance5, supported.maintenance5,
                        &VkPhysicalDeviceMaintenance5Features::maintenance5);
    }
}

void turnOn(VkBool32 &feature, VkBool32 requested) {
    if (requested != VK_FALSE) feature = VK_TRUE;
}

// Turns on each VkBool32 between byte `begin` and byte `end` of `features` that is on in `requested`.
void turnOnRange(void *features, const void *requested, size_t begin, size_t end) {
    auto *into = static_cast<std::byte *>(features);
    auto *from = static_cast<const std::byte *>(requested);
    for (size_t offset = begin; offset + sizeof(VkBool32) <= end; offset += sizeof(VkBool32)) {
        VkBool32 value;
        std::memcpy(&value, from + offset, sizeof(value));
        if (value == VK_FALSE) continue;
        value = VK_TRUE;
        std::memcpy(into + offset, &value, sizeof(value));
    }
}

void turnOnAll(VkPhysicalDeviceFeatures &features, const VkPhysicalDeviceFeatures &requested) {
    turnOnRange(&features, &requested, 0, sizeof(VkPhysicalDeviceFeatures));
}

// Past their header (sType, pNext), the core feature structures hold nothing but VkBool32 fields.
static_assert(offsetof(VkPhysicalDeviceVulkan11Features, storageBuffer16BitAccess) == sizeof(VkBaseOutStructure));

template <typename T>
    requires requires(T &features) { features.pNext; }
void turnOnAll(T &features, const T &requested) {
    turnOnRange(&features, &requested, sizeof(VkBaseOutStructure), sizeof(T));
}

template <typename T>
const T &as(const VkBaseInStructure *node) {
    return *reinterpret_cast<const T *>(node);
}

// Minecraft's own device request, folded into the renderer's. Its core features turn on the same fields of the
// renderer's structures, the ones in structures that Vulkan 1.2 and 1.3 promoted included (a device may not be given a
// feature in both forms); the extension structures the renderer does not fill are copied to the end of the chain.
class MinecraftFeatures {
  public:
    void merge(const VkDeviceCreateInfo &request, FeatureChain &chain) {
        VkPhysicalDeviceFeatures &core = chain.features2.features;
        if (request.pEnabledFeatures != nullptr) turnOnAll(core, *request.pEnabledFeatures);

        for (auto *node = static_cast<const VkBaseInStructure *>(request.pNext); node != nullptr; node = node->pNext) {
            switch (node->sType) {
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
                    turnOnAll(core, as<VkPhysicalDeviceFeatures2>(node).features);
                    break;
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
                    turnOnAll(chain.vulkan11, as<VkPhysicalDeviceVulkan11Features>(node));
                    break;
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
                    turnOnAll(chain.vulkan12, as<VkPhysicalDeviceVulkan12Features>(node));
                    break;
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
                    turnOnAll(chain.vulkan13, as<VkPhysicalDeviceVulkan13Features>(node));
                    break;
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES:
                    turnOn(chain.vulkan11.shaderDrawParameters,
                           as<VkPhysicalDeviceShaderDrawParametersFeatures>(node).shaderDrawParameters);
                    break;
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
                    turnOn(chain.vulkan12.timelineSemaphore,
                           as<VkPhysicalDeviceTimelineSemaphoreFeatures>(node).timelineSemaphore);
                    break;
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES:
                    turnOn(chain.vulkan12.hostQueryReset,
                           as<VkPhysicalDeviceHostQueryResetFeatures>(node).hostQueryReset);
                    break;
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES:
                    turnOn(chain.vulkan13.synchronization2,
                           as<VkPhysicalDeviceSynchronization2Features>(node).synchronization2);
                    break;
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES:
                    turnOn(chain.vulkan13.dynamicRendering,
                           as<VkPhysicalDeviceDynamicRenderingFeatures>(node).dynamicRendering);
                    break;
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT:
                    copy<VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT>(node, chain);
                    break;
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT:
                    copy<VkPhysicalDeviceMultiDrawFeaturesEXT>(node, chain);
                    break;
                default:
                    deviceError() << "unknown feature struct in Minecraft device chain, sType=" << node->sType
                                  << "; skipping" << std::endl;
                    break;
            }
        }
    }

  private:
    template <typename T>
    void copy(const VkBaseInStructure *node, FeatureChain &chain) {
        auto structure = std::make_shared<T>(as<T>(node));
        chain.append(structure.get());
        copies_.push_back(std::move(structure));
    }

    std::vector<std::shared_ptr<void>> copies_;
};

// ---- pipeline cache file

// Where the pipeline cache is kept. Set by the mod at start-up, read by the thread that builds the first pipeline.
struct PipelineCacheFile {
    std::mutex mutex;
    std::filesystem::path path;
};

PipelineCacheFile &pipelineCacheFile() {
    static PipelineCacheFile file;
    return file;
}

std::filesystem::path pipelineCachePath() {
    PipelineCacheFile &file = pipelineCacheFile();
    std::lock_guard<std::mutex> lock(file.mutex);
    return file.path;
}

std::vector<char> readFile(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamsize size = in.tellg();
    if (size <= 0) return {};
    std::vector<char> data(static_cast<size_t>(size));
    in.seekg(0);
    if (!in.read(data.data(), size)) return {};
    return data;
}

// Writes through a temporary file beside the target and moves it over, so a crash mid-write leaves the old file whole.
void replaceFile(const std::filesystem::path &file, std::span<const char> data) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.close();
        if (out.fail()) {
            std::filesystem::remove(temporary, error);
            return;
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) std::filesystem::remove(temporary, error);
}

// Whether cache data was written by this GPU and driver. The driver would start empty on data from another one anyway;
// checking first also keeps a file with a damaged header from reaching it.
bool cacheDataFits(std::span<const char> data, const VkPhysicalDeviceProperties &properties) {
    VkPipelineCacheHeaderVersionOne header{};
    if (data.size() < sizeof(header)) return false;
    std::memcpy(&header, data.data(), sizeof(header));
    return header.headerSize >= sizeof(header) && header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           header.vendorID == properties.vendorID && header.deviceID == properties.deviceID &&
           std::memcmp(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

} // namespace

// ---- creation

VkResult Device::createMerged(VkPhysicalDevice physicalDeviceHandle,
                              const VkDeviceCreateInfo *baseInfo,
                              const VkAllocationCallbacks *allocator,
                              VkDevice *outDevice) {
    if (baseInfo == nullptr || outDevice == nullptr) return VK_ERROR_INITIALIZATION_FAILED;

    // The upscaler SDKs are asked through the renderer's wrappers.
    auto instance = Instance::create(Instance::lastCreatedInstance());
    auto physicalDevice = PhysicalDevice::create(instance, physicalDeviceHandle, 0, 0);

    if (!PhysicalDevice::isRayTracingCapable(physicalDeviceHandle)) {
        deviceError() << "selected GPU does not support hardware ray tracing" << std::endl;
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    DeviceExtensions extensions(physicalDeviceHandle);
    for (uint32_t i = 0; i < baseInfo->enabledExtensionCount; i++) {
        extensions.request(baseInfo->ppEnabledExtensionNames[i]);
    }
    for (const char *name : kRendererExtensions) extensions.request(name);

    // NVIDIA Reflex (VK_NV_low_latency2; the present ids tie its markers to presented frames).
    if (framegen::Reflex::deviceSupports(physicalDeviceHandle)) {
        extensions.request(VK_NV_LOW_LATENCY_2_EXTENSION_NAME);
        extensions.request(VK_KHR_PRESENT_ID_EXTENSION_NAME);
    }

    // DLSS frame generation, driven through NGX directly.
    const Requirement dlssFrameGeneration = ngxRequirement([&](std::vector<VkExtensionProperties> &list) {
        return NgxContext::getDlssFrameGenerationRequiredDeviceExtensions(instance, physicalDevice, list);
    });
    extensions.requestNgx(dlssFrameGeneration);
    // The one extension FSR frame generation needs that the renderer does not request anyway.
    extensions.request(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);

    const Requirement dlss = ngxRequirement([&](std::vector<VkExtensionProperties> &list) {
        return NgxContext::getDlssRRRequiredDeviceExtensions(instance, physicalDevice, list);
    });
    if (!dlss.known) deviceLog() << "dlss device extensions unavailable; skipping." << std::endl;
    extensions.requestNgx(dlss);

    // Every extension XeSS lists is requested, VK_EXT_buffer_device_address included.
    Requirement xess;
#ifdef PRISM_ENABLE_XESS
    std::vector<const char *> xessExtensions;
    if (prism::XeSSWrapper::getRequiredDeviceExtensions(instance->vkInstance(), physicalDeviceHandle, xessExtensions)) {
        xess.known = true;
        xess.extensions.assign(xessExtensions.begin(), xessExtensions.end());
    } else {
        deviceLog() << "xess device extensions unavailable; skipping." << std::endl;
    }
#endif
    for (const std::string &name : xess.extensions) extensions.request(name);

    if (extensions.offered(VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME)) {
        extensions.request(VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME);
    }
    // Shader execution reordering: the GPU regroups rays by what they hit before shading them (RTX 40 and newer).
    const bool reorderOffered = extensions.offered(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME);
    if (reorderOffered) extensions.request(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME);
    if (diagnostics::checkpointsRequested() && extensions.offered(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME)) {
        extensions.request(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
    }

    const std::vector<const char *> enabledExtensions = extensions.enable();
    const bool dlssCompatible = instance->isDlssInstanceExtensionsCompatible() && extensions.serves(dlss);
    const bool dlssFrameGenerationCompatible =
        instance->isDlssFrameGenerationInstanceExtensionsCompatible() && extensions.serves(dlssFrameGeneration);
    const bool fsrFrameGenerationCompatible =
        std::ranges::all_of(kFsrFrameGenerationExtensions, [&](const char *name) { return extensions.enabled(name); });
    bool xessCompatible = instance->isXessInstanceExtensionsCompatible() && extensions.serves(xess);

    FeatureChain supported(reorderOffered);
    vkGetPhysicalDeviceFeatures2(physicalDeviceHandle, &supported.features2);

    const bool reorderEnabled = extensions.enabled(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME) &&
                                supported.invocationReorder.rayTracingInvocationReorder == VK_TRUE;
    FeatureChain enabled(reorderEnabled);
    enabled.invocationReorder.rayTracingInvocationReorder = reorderEnabled ? VK_TRUE : VK_FALSE;
    enableRendererFeatures(enabled, supported, extensions);

    MinecraftFeatures minecraft;
    minecraft.merge(*baseInfo, enabled);

#ifdef PRISM_ENABLE_XESS
    if (xessCompatible) {
        // XeSS turns on what it needs in the chain.
        void *chain = &enabled.features2;
        if (!prism::XeSSWrapper::getRequiredDeviceFeatures(instance->vkInstance(), physicalDeviceHandle, &chain)) {
            xessCompatible = false;
            deviceLog() << "xess device feature requirements are not fully satisfied." << std::endl;
        }
    }
#endif

    // Reflex ties its latency markers to presented frames by present id.
    VkPhysicalDevicePresentIdFeaturesKHR presentId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    bool reflexEnabled = false;
    if (extensions.enabled(VK_NV_LOW_LATENCY_2_EXTENSION_NAME) &&
        extensions.enabled(VK_KHR_PRESENT_ID_EXTENSION_NAME)) {
        VkPhysicalDevicePresentIdFeaturesKHR supportedPresentId{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
        VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supportedPresentId};
        vkGetPhysicalDeviceFeatures2(physicalDeviceHandle, &query);
        reflexEnabled = supportedPresentId.presentId == VK_TRUE;
        if (reflexEnabled) {
            presentId.presentId = VK_TRUE;
            presentId.pNext = enabled.features2.pNext;
            enabled.features2.pNext = &presentId;
        }
    }

    // Minecraft's queues, layers and flags as it asked for them.
    VkDeviceCreateInfo createInfo = *baseInfo;
    createInfo.pNext = &enabled.features2;
    createInfo.pEnabledFeatures = nullptr;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    const VkResult result = vkCreateDevice(physicalDeviceHandle, &createInfo, allocator, outDevice);
    if (result != VK_SUCCESS) {
        deviceError() << "vkCreateDevice failed: " << result << std::endl;
        return result;
    }

    volkLoadDevice(*outDevice);
    framegen::Reflex::setDevice(*outDevice, reflexEnabled);
    installQueueLockHooks();
    diagnostics::setCheckpointsEnabled(extensions.enabled(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME));

    g_created = {
        .handle = *outDevice,
        .dlss = dlssCompatible,
        .dlssFrameGeneration = dlssFrameGenerationCompatible,
        .fsrFrameGeneration = fsrFrameGenerationCompatible,
        .shaderExecutionReordering = reorderEnabled,
        .xess = xessCompatible,
    };
    deviceLog() << "created shared device with " << enabledExtensions.size() << " extensions (dlss=" << dlssCompatible
                << ", dlss frame generation=" << dlssFrameGenerationCompatible
                << ", fsr frame generation=" << fsrFrameGenerationCompatible << ", ser=" << reorderEnabled
                << ", xess=" << xessCompatible << ")" << std::endl;
    return VK_SUCCESS;
}

// ---- Device

Device::Device(std::shared_ptr<Instance> instance,
               std::shared_ptr<PhysicalDevice> physicalDevice,
               VkDevice device,
               VkQueue mainQueue,
               VkQueue secondaryQueue)
    : instance_(std::move(instance)),
      physicalDevice_(std::move(physicalDevice)),
      device_(device),
      mainQueue_(mainQueue),
      secondaryQueue_(secondaryQueue) {
    // createMerged loaded the device it made already. Loading it again would drop the queue lock hooks until they are
    // installed again, while other threads may be submitting.
    if (volkGetLoadedDevice() != device_) volkLoadDevice(device_);
    installQueueLockHooks();
}

Device::~Device() {
    // The device is Minecraft's; only the pipeline cache is the renderer's.
    savePipelineCache();
    if (pipelineCache_ != VK_NULL_HANDLE) vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
}

VkDevice &Device::vkDevice() {
    return device_;
}

VkQueue &Device::mainVkQueue() {
    return mainQueue_;
}

VkQueue &Device::secondaryQueue() {
    return secondaryQueue_;
}

bool Device::isDlssDeviceExtensionsCompatible() const {
    return g_created.handle == device_ && g_created.dlss;
}

bool Device::isXessDeviceExtensionsCompatible() const {
    return g_created.handle == device_ && g_created.xess;
}

bool Device::isDlssFrameGenerationDeviceExtensionsCompatible() const {
    return g_created.handle == device_ && g_created.dlssFrameGeneration;
}

bool Device::isFsrFrameGenerationDeviceExtensionsCompatible() const {
    return g_created.handle == device_ && g_created.fsrFrameGeneration;
}

bool Device::isShaderExecutionReorderingEnabled() const {
    return g_created.handle == device_ && g_created.shaderExecutionReordering;
}

// ---- pipeline cache

void Device::setPipelineCacheFile(const std::filesystem::path &file) {
    PipelineCacheFile &setting = pipelineCacheFile();
    std::lock_guard<std::mutex> lock(setting.mutex);
    setting.path = file;
}

VkPipelineCache Device::pipelineCache() {
    std::lock_guard<std::mutex> lock(pipelineCacheMutex_);
    if (pipelineCacheAttempted_) return pipelineCache_;
    pipelineCacheAttempted_ = true;

    const std::filesystem::path file = pipelineCachePath();
    std::vector<char> data = file.empty() ? std::vector<char>() : readFile(file);
    if (!data.empty() && physicalDevice_ != nullptr && !cacheDataFits(data, physicalDevice_->properties())) {
        deviceLog() << "pipeline cache file does not match this GPU and driver; starting empty" << std::endl;
        data.clear();
    }

    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = data.size();
    info.pInitialData = data.empty() ? nullptr : data.data();
    VkResult result = vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_);
    if (result != VK_SUCCESS && !data.empty()) {
        // Data the driver will not take (a damaged file): start empty.
        data.clear();
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        result = vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_);
    }
    if (result != VK_SUCCESS) {
        pipelineCache_ = VK_NULL_HANDLE;
        deviceError() << "cannot create the pipeline cache (" << result << "); pipelines are built without one"
                      << std::endl;
    } else {
        deviceLog() << "pipeline cache loaded: " << data.size() / 1024 << " KiB" << std::endl;
    }
    return pipelineCache_;
}

void Device::savePipelineCache() {
    std::lock_guard<std::mutex> saving(pipelineCacheSaveMutex_);
    VkPipelineCache cache;
    {
        std::lock_guard<std::mutex> lock(pipelineCacheMutex_);
        cache = pipelineCache_;
    }
    const std::filesystem::path file = pipelineCachePath();
    if (cache == VK_NULL_HANDLE || file.empty()) return;

    size_t size = 0;
    if (vkGetPipelineCacheData(device_, cache, &size, nullptr) != VK_SUCCESS || size == 0) return;
    std::vector<char> data(size);
    if (vkGetPipelineCacheData(device_, cache, &size, data.data()) != VK_SUCCESS) return;
    data.resize(size);
    replaceFile(file, data);
}

} // namespace gpu
