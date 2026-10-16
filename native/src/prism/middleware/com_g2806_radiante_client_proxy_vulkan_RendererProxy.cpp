#include "prism/all_extern.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/instance.hpp"
#include "prism/gpu/physical_device.hpp"
#include "prism/gpu/queue_lock.hpp"
#include "prism/middleware/jni_support.hpp"
#include "prism/render/framegen/frame_generation.hpp"
#include "prism/render/framegen/native_frame_generation.hpp"
#include "prism/render/framegen/reflex.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/render/textures.hpp"
#include "prism/render/world.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace jni = prism::jni;
using framegen::FrameGeneration;
using framegen::NativeFrameGeneration;
using framegen::Reflex;

namespace {

// Hands the frame's command buffers back in Java's array, as many as it holds, through a buffer on the stack: a
// frame allocates nothing here.
jint writeCommandBuffers(JNIEnv *env, jlongArray out, const std::vector<VkCommandBuffer> &commandBuffers) {
    if (out == nullptr) return 0;
    const jsize count = std::min(env->GetArrayLength(out), static_cast<jsize>(commandBuffers.size()));
    constexpr jsize kBatch = 8;
    jlong batch[kBatch];
    for (jsize first = 0; first < count; first += kBatch) {
        const jsize length = std::min(kBatch, count - first);
        for (jsize i = 0; i < length; i++) batch[i] = jni::toLong(commandBuffers[static_cast<size_t>(first + i)]);
        env->SetLongArrayRegion(out, first, length, batch);
    }
    return count;
}

// How far `done` is through a non-zero `total`, in percent, at most 100.
jint percent(uint32_t done, uint32_t total) {
    return static_cast<jint>(std::min<uint64_t>(100, 100ull * done / total));
}

// Log text for Java, which takes null for none.
jstring textOrNull(JNIEnv *env, const std::string &text) {
    return text.empty() ? nullptr : env->NewStringUTF(text.c_str());
}

} // namespace

extern "C" {

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_initFolderPath(JNIEnv *env,
                                                                                                jclass,
                                                                                                jstring folderPath) {
    if (folderPath == nullptr) return;
    jni::guard("[Radiante] initFolderPath", [&] {
        Renderer::folderPath = jni::utf16(env, folderPath);
        gpu::Device::setPipelineCacheFile(Renderer::folderPath / "cache" / "pipeline_cache.bin");
    });
}

JNIEXPORT jint JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_createInstance(
    JNIEnv *, jclass, jlong createInfo, jlong allocator, jlong outInstance) {
    return jni::guard("[Radiante] createInstance", VK_ERROR_INITIALIZATION_FAILED, [&] {
        return static_cast<jint>(gpu::Instance::createMerged(jni::as<const VkInstanceCreateInfo *>(createInfo),
                                                             jni::as<const VkAllocationCallbacks *>(allocator),
                                                             jni::as<VkInstance *>(outInstance)));
    });
}

JNIEXPORT jint JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_createDevice(
    JNIEnv *, jclass, jlong physicalDevice, jlong createInfo, jlong allocator, jlong outDevice) {
    return jni::guard("[Radiante] createDevice", VK_ERROR_INITIALIZATION_FAILED, [&] {
        return static_cast<jint>(gpu::Device::createMerged(
            jni::as<VkPhysicalDevice>(physicalDevice), jni::as<const VkDeviceCreateInfo *>(createInfo),
            jni::as<const VkAllocationCallbacks *>(allocator), jni::as<VkDevice *>(outDevice)));
    });
}

JNIEXPORT jboolean JNICALL
Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_isRayTracingCapable(JNIEnv *, jclass, jlong physicalDevice) {
    return jni::boolean(jni::guard("[Radiante] isRayTracingCapable", false, [physicalDevice] {
        return gpu::PhysicalDevice::isRayTracingCapable(jni::as<VkPhysicalDevice>(physicalDevice));
    }));
}

JNIEXPORT jboolean JNICALL
Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_initRenderer(JNIEnv *,
                                                                       jclass,
                                                                       jlong instance,
                                                                       jlong physicalDevice,
                                                                       jlong device,
                                                                       jlong mainQueue,
                                                                       jint mainQueueFamily,
                                                                       jlong secondaryQueue,
                                                                       jint secondaryQueueFamily,
                                                                       jint width,
                                                                       jint height) {
    return jni::boolean(jni::guard("[Radiante] initRenderer", false, [&] {
        Renderer::init(SharedDeviceHandles{
            .instance = jni::as<VkInstance>(instance),
            .physicalDevice = jni::as<VkPhysicalDevice>(physicalDevice),
            .device = jni::as<VkDevice>(device),
            .mainQueue = jni::as<VkQueue>(mainQueue),
            .mainQueueFamily = static_cast<uint32_t>(mainQueueFamily),
            .secondaryQueue = jni::as<VkQueue>(secondaryQueue),
            .secondaryQueueFamily = static_cast<uint32_t>(secondaryQueueFamily),
            .width = static_cast<uint32_t>(std::max(width, 1)),
            .height = static_cast<uint32_t>(std::max(height, 1)),
        });
        Renderer::instance().framework()->acquireContext();
        return true;
    }));
}

// Records the frame into Java's target image and returns how many command buffers it wrote to outCommandBuffers;
// Java submits them in that order. 0 when there is nothing to submit.
JNIEXPORT jint JNICALL
Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_renderFrame(JNIEnv *env,
                                                                      jclass,
                                                                      jlong target,
                                                                      jint width,
                                                                      jint height,
                                                                      jint traceWidth,
                                                                      jint traceHeight,
                                                                      jint format,
                                                                      jlongArray outCommandBuffers) {
    auto framework = jni::framework();
    if (framework == nullptr) return 0;
    const auto commandBuffers = jni::guard("[Radiante] renderFrame", std::vector<VkCommandBuffer>(), [&] {
        return framework->renderFrame(jni::as<VkImage>(target), static_cast<uint32_t>(width),
                                      static_cast<uint32_t>(height), static_cast<uint32_t>(std::max(traceWidth, 1)),
                                      static_cast<uint32_t>(std::max(traceHeight, 1)), static_cast<VkFormat>(format));
    });
    return writeCommandBuffers(env, outCommandBuffers, commandBuffers);
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_markSubmitted(JNIEnv *,
                                                                                               jclass,
                                                                                               jlong semaphore,
                                                                                               jlong value) {
    jni::guard("[Radiante] markSubmitted", [&] {
        auto framework = jni::framework();
        if (framework == nullptr) return;
        framework->markSubmitted(jni::as<VkSemaphore>(semaphore), static_cast<uint64_t>(value));
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_flushTextureUploads(JNIEnv *, jclass) {
    jni::guard("[Radiante] flushTextureUploads", [] {
        if (auto textures = jni::textures()) textures->performQueuedUpload();
    });
}

// Minecraft's own queue operations take the lock every native one takes, and name themselves for the watchdog's
// report. These two are left unguarded on purpose: after a failed lock, going on would run the queue unlocked and then
// release a lock this thread does not hold.
JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_lockQueue(JNIEnv *, jclass) {
    gpu::queueMutex().lock();
    gpu::queueOp().store("minecraft queue op");
    gpu::queueOpStart().store(std::chrono::steady_clock::now().time_since_epoch().count());
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_unlockQueue(JNIEnv *, jclass) {
    gpu::queueOp().store(nullptr);
    gpu::queueMutex().unlock();
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_close(JNIEnv *, jclass) {
    jni::guard("[Radiante] close", [] {
        if (Renderer *renderer = Renderer::try_instance()) renderer->close();
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_setLoggingEnabled(JNIEnv *,
                                                                                                   jclass,
                                                                                                   jboolean enabled) {
    radiante::setLoggingEnabled(jni::isTrue(enabled));
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_shouldRenderWorld(
    JNIEnv *, jclass, jboolean shouldRenderWorld) {
    jni::guard("[Radiante] shouldRenderWorld", [shouldRenderWorld] {
        if (auto world = jni::world()) world->shouldRender() = jni::isTrue(shouldRenderWorld);
    });
}

JNIEXPORT jint JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_maxGeneratedFrames(JNIEnv *, jclass) {
    return jni::guard("[Radiante] maxGeneratedFrames", 0,
                      [] { return static_cast<jint>(NativeFrameGeneration::instance().maxGeneratedFrames()); });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_setGeneratedFrames(JNIEnv *,
                                                                                                    jclass,
                                                                                                    jint frames) {
    jni::guard("[Radiante] setGeneratedFrames", [frames] {
        NativeFrameGeneration::instance().setGeneratedFrames(static_cast<uint32_t>(std::max(frames, 0)));
    });
}

JNIEXPORT jint JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_presentedFrameRate(JNIEnv *, jclass) {
    return jni::guard("[Radiante] presentedFrameRate", 0,
                      [] { return static_cast<jint>(NativeFrameGeneration::instance().presentedFrameRate()); });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_setReflexEnabled(JNIEnv *,
                                                                                                  jclass,
                                                                                                  jboolean enabled) {
    jni::guard("[Radiante] setReflexEnabled", [enabled] { Reflex::setEnabled(jni::isTrue(enabled)); });
}

JNIEXPORT jboolean JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_isReflexSupported(JNIEnv *,
                                                                                                       jclass) {
    return jni::boolean(jni::guard("[Radiante] isReflexSupported", false, [] { return Reflex::isSupported(); }));
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_reflexChainSwapchainCreate(
    JNIEnv *, jclass, jlong createInfo) {
    jni::guard("[Radiante] reflexChainSwapchainCreate",
               [createInfo] { Reflex::chainSwapchainCreate(jni::as<void *>(createInfo)); });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_reflexSetSwapchain(JNIEnv *,
                                                                                                    jclass,
                                                                                                    jlong swapchain) {
    jni::guard("[Radiante] reflexSetSwapchain",
               [swapchain] { Reflex::setSwapchain(jni::as<VkSwapchainKHR>(swapchain)); });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_reflexChainPresent(JNIEnv *,
                                                                                                    jclass,
                                                                                                    jlong presentInfo) {
    jni::guard("[Radiante] reflexChainPresent", [presentInfo] { Reflex::chainPresent(jni::as<void *>(presentInfo)); });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_setOverlayDepthTarget(
    JNIEnv *, jclass, jlong image, jint format, jfloat zScale, jfloat zOffset, jfloat wScale, jfloat wOffset) {
    jni::guard("[Radiante] setOverlayDepthTarget", [&] {
        auto framework = jni::framework();
        if (framework == nullptr) return;
        const float projection[4] = {zScale, zOffset, wScale, wOffset};
        framework->setOverlayDepthTarget(jni::as<VkImage>(image), static_cast<VkFormat>(format), projection);
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_setFrameGenerationImages(
    JNIEnv *, jclass, jint depthSlot, jint motionVectorSlot) {
    jni::guard("[Radiante] setFrameGenerationImages",
               [=] { FrameGeneration::setImageSlots(depthSlot, motionVectorSlot); });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_beginFrameGenerationFrame(JNIEnv *,
                                                                                                           jclass) {
    jni::guard("[Radiante] beginFrameGenerationFrame", [] { FrameGeneration::beginClientFrame(); });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_frameGenerationMarker(JNIEnv *,
                                                                                                       jclass,
                                                                                                       jint marker) {
    jni::guard("[Radiante] frameGenerationMarker", [marker] { FrameGeneration::marker(marker); });
}

JNIEXPORT jstring JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_drainNativeErrors(JNIEnv *env,
                                                                                                      jclass) {
    return textOrNull(env, jni::guard("[Radiante] drainNativeErrors", std::string(), radiante::drainErrors));
}

JNIEXPORT jstring JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_drainNativeInfo(JNIEnv *env,
                                                                                                    jclass) {
    return textOrNull(env, jni::guard("[Radiante] drainNativeInfo", std::string(), radiante::drainInfo));
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_rebuildPendingPipeline(JNIEnv *,
                                                                                                        jclass) {
    jni::guard("[Radiante] pipeline rebuild", [] {
        if (auto framework = jni::framework()) framework->recreatePending();
    });
}

// 0-100 while the shaders compile, then 1000-1100 while the passes' pipelines are created from them.
JNIEXPORT jint JNICALL Java_com_g2806_radiante_client_proxy_vulkan_RendererProxy_rebuildProgress(JNIEnv *, jclass) {
    const uint32_t passes = Pipeline::rebuildPassesTotal.load();
    if (passes > 0) return 1000 + percent(Pipeline::rebuildPassesDone.load(), passes);
    const uint32_t shaders = Pipeline::rebuildShadersTotal.load();
    return shaders > 0 ? percent(Pipeline::rebuildShadersDone.load(), shaders) : 0;
}

} // extern "C"
