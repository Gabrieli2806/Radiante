#include "prism/gpu/instance.hpp"

#include "prism/render/modules/world/dlss/dlss_wrapper.hpp"
#include "prism/render/modules/world/xess_upscaler/xess_wrapper.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace gpu {
namespace {

std::ostream &instanceLog() {
    return radiante::out() << "[Instance] ";
}

std::ostream &instanceError() {
    return radiante::err() << "[Instance] ";
}

// What createMerged enabled on the instance it made last. A wrapper answers for its own instance only.
struct CreatedInstance {
    VkInstance handle = VK_NULL_HANDLE;
    uint32_t apiVersion = VK_API_VERSION_1_3;
    bool dlss = false;
    bool dlssFrameGeneration = false;
    bool xess = false;
};

CreatedInstance g_created;

// The instance extensions an SDK (DLSS, DLSS frame generation, XeSS) asked for. `known` is false when it could not
// tell; that feature is then left off.
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

std::unordered_set<std::string> availableExtensions() {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    extensions.resize(count);

    std::unordered_set<std::string> names;
    names.reserve(extensions.size());
    for (const VkExtensionProperties &extension : extensions) names.emplace(extension.extensionName);
    return names;
}

} // namespace

VkResult Instance::createMerged(const VkInstanceCreateInfo *baseInfo,
                                const VkAllocationCallbacks *allocator,
                                VkInstance *outInstance) {
    if (baseInfo == nullptr || outInstance == nullptr) return VK_ERROR_INITIALIZATION_FAILED;

    // volk finds the Vulkan loader once; the instance functions are loaded for each instance made.
    if (volkGetLoadedInstance() == VK_NULL_HANDLE && volkInitialize() != VK_SUCCESS) {
        instanceError() << "volkInitialize failed" << std::endl;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // The renderer needs Vulkan 1.3.
    VkApplicationInfo appInfo{};
    if (baseInfo->pApplicationInfo != nullptr) appInfo = *baseInfo->pApplicationInfo;
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = std::max<uint32_t>(appInfo.apiVersion, VK_API_VERSION_1_3);

    // Minecraft's extensions and the upscalers', each once.
    std::set<std::string> wanted(baseInfo->ppEnabledExtensionNames,
                                 baseInfo->ppEnabledExtensionNames + baseInfo->enabledExtensionCount);
    auto want = [&wanted](const Requirement &requirement) {
        wanted.insert(requirement.extensions.begin(), requirement.extensions.end());
    };

    const Requirement dlss = ngxRequirement([](std::vector<VkExtensionProperties> &extensions) {
        return NgxContext::getDlssRRRequiredInstanceExtensions(extensions);
    });
    if (!dlss.known) instanceLog() << "failed to query dlss instance extensions; skipping." << std::endl;
    want(dlss);

    Requirement xess;
#ifdef PRISM_ENABLE_XESS
    std::vector<const char *> xessExtensions;
    uint32_t xessApiVersion = 0;
    if (prism::XeSSWrapper::getRequiredInstanceExtensions(xessExtensions, &xessApiVersion)) {
        xess.known = true;
        xess.extensions.assign(xessExtensions.begin(), xessExtensions.end());
        appInfo.apiVersion = std::max(appInfo.apiVersion, xessApiVersion);
    } else {
        instanceLog() << "xess instance extensions unavailable; skipping." << std::endl;
    }
#endif
    want(xess);

    // DLSS frame generation, driven through NGX directly.
    const Requirement dlssFrameGeneration = ngxRequirement([](std::vector<VkExtensionProperties> &extensions) {
        return NgxContext::getDlssFrameGenerationRequiredInstanceExtensions(extensions);
    });
    want(dlssFrameGeneration);

    wanted.insert(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    // Lets the window offer HDR colour spaces (scRGB, HDR10); used only when the HDR option picks one.
    wanted.insert(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);

    const std::unordered_set<std::string> available = availableExtensions();
    auto usable = [&](const Requirement &requirement) {
        return requirement.known && std::ranges::all_of(requirement.extensions, [&](const std::string &name) {
                   return available.contains(name);
               });
    };

    std::vector<const char *> enabled;
    enabled.reserve(wanted.size());
    for (const std::string &name : wanted) {
        if (available.contains(name)) {
            enabled.push_back(name.c_str());
        } else {
            instanceError() << "extension not supported, skipping: " << name << std::endl;
        }
    }

    VkInstanceCreateInfo createInfo = *baseInfo;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    createInfo.ppEnabledExtensionNames = enabled.data();

    const VkResult result = vkCreateInstance(&createInfo, allocator, outInstance);
    if (result != VK_SUCCESS) {
        instanceError() << "vkCreateInstance failed: " << result << std::endl;
        return result;
    }

    volkLoadInstanceOnly(*outInstance);
    g_created = {
        .handle = *outInstance,
        .apiVersion = appInfo.apiVersion,
        .dlss = usable(dlss),
        .dlssFrameGeneration = usable(dlssFrameGeneration),
        .xess = usable(xess),
    };
    instanceLog() << "created shared instance (api " << VK_API_VERSION_MAJOR(appInfo.apiVersion) << "."
                  << VK_API_VERSION_MINOR(appInfo.apiVersion) << ", dlss=" << g_created.dlss
                  << ", xess=" << g_created.xess << ")" << std::endl;
    return VK_SUCCESS;
}

VkInstance Instance::lastCreatedInstance() {
    return g_created.handle;
}

Instance::Instance(VkInstance instance) : instance_(instance) {
    // Loading the instance volk already has would only rewrite its function pointers while other threads use them.
    if (volkGetLoadedInstance() != instance_) volkLoadInstanceOnly(instance_);
}

VkInstance &Instance::vkInstance() {
    return instance_;
}

uint32_t Instance::apiVersion() const {
    return g_created.apiVersion;
}

bool Instance::isDlssInstanceExtensionsCompatible() const {
    return g_created.handle == instance_ && g_created.dlss;
}

bool Instance::isXessInstanceExtensionsCompatible() const {
    return g_created.handle == instance_ && g_created.xess;
}

bool Instance::isDlssFrameGenerationInstanceExtensionsCompatible() const {
    return g_created.handle == instance_ && g_created.dlssFrameGeneration;
}

} // namespace gpu
