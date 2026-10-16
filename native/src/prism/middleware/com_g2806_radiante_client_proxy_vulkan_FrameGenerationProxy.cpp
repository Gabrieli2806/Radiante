#include "prism/all_extern.hpp"
#include "prism/middleware/jni_support.hpp"
#include "prism/render/framegen/native_frame_generation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jni = prism::jni;
using framegen::NativeFrameGeneration;

namespace {

// The backend a Java constant names, and Off (automatic) for a number that names none.
NativeFrameGeneration::Backend backendFrom(jint value) {
    const bool known = value >= 0 && value <= static_cast<jint>(NativeFrameGeneration::Backend::Fsr);
    return known ? static_cast<NativeFrameGeneration::Backend>(value) : NativeFrameGeneration::Backend::Off;
}

// The swapchain's images, which Java hands over with every present, read into a list kept for the next one.
const std::vector<VkImage> &swapchainImagesFrom(JNIEnv *env, jlongArray array) {
    thread_local std::vector<VkImage> images;
    images.clear();
    if (array == nullptr) return images;
    const jsize count = env->GetArrayLength(array);
    images.resize(static_cast<size_t>(count));
    constexpr jsize kBatch = 8;
    jlong batch[kBatch];
    for (jsize first = 0; first < count; first += kBatch) {
        const jsize length = std::min(kBatch, count - first);
        env->GetLongArrayRegion(array, first, length, batch);
        for (jsize i = 0; i < length; i++) images[static_cast<size_t>(first + i)] = jni::as<VkImage>(batch[i]);
    }
    return images;
}

} // namespace

extern "C" {

JNIEXPORT jint JNICALL Java_com_g2806_radiante_client_proxy_vulkan_FrameGenerationProxy_backend(JNIEnv *, jclass) {
    return jni::guard("[Frame Generation] backend", 0,
                      [] { return static_cast<jint>(NativeFrameGeneration::instance().backend()); });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_FrameGenerationProxy_setPreference(JNIEnv *,
                                                                                                      jclass,
                                                                                                      jint backend) {
    jni::guard("[Frame Generation] setPreference",
               [backend] { NativeFrameGeneration::instance().setPreference(backendFrom(backend)); });
}

JNIEXPORT jboolean JNICALL Java_com_g2806_radiante_client_proxy_vulkan_FrameGenerationProxy_isAvailable(JNIEnv *,
                                                                                                        jclass,
                                                                                                        jint backend) {
    return jni::boolean(jni::guard("[Frame Generation] isAvailable", false, [backend] {
        return NativeFrameGeneration::instance().available(backendFrom(backend));
    }));
}

JNIEXPORT jint JNICALL
Java_com_g2806_radiante_client_proxy_vulkan_FrameGenerationProxy_maxGeneratedFramesFor(JNIEnv *, jclass, jint backend) {
    return jni::guard("[Frame Generation] maxGeneratedFramesFor", 0, [backend] {
        return static_cast<jint>(NativeFrameGeneration::instance().maxGeneratedFramesFor(backendFrom(backend)));
    });
}

JNIEXPORT jboolean JNICALL
Java_com_g2806_radiante_client_proxy_vulkan_FrameGenerationProxy_evaluateAndBlit(JNIEnv *,
                                                                                 jclass,
                                                                                 jlong commandBuffer,
                                                                                 jlong finalImage,
                                                                                 jint finalFormat,
                                                                                 jint width,
                                                                                 jint height,
                                                                                 jlong swapchainImage,
                                                                                 jint swapchainWidth,
                                                                                 jint swapchainHeight,
                                                                                 jboolean flipY) {
    return jni::boolean(jni::guard("[Frame Generation] evaluation", false, [&] {
        return NativeFrameGeneration::instance().evaluateAndBlit(
            jni::as<VkCommandBuffer>(commandBuffer), jni::as<VkImage>(finalImage), static_cast<VkFormat>(finalFormat),
            static_cast<uint32_t>(width), static_cast<uint32_t>(height), jni::as<VkImage>(swapchainImage),
            static_cast<uint32_t>(swapchainWidth), static_cast<uint32_t>(swapchainHeight), jni::isTrue(flipY));
    }));
}

JNIEXPORT jint JNICALL
Java_com_g2806_radiante_client_proxy_vulkan_FrameGenerationProxy_presentPending(JNIEnv *env,
                                                                                jclass,
                                                                                jlong swapchain,
                                                                                jlongArray swapchainImages,
                                                                                jlong queue,
                                                                                jint swapchainWidth,
                                                                                jint swapchainHeight) {
    return jni::guard("[Frame Generation] present", 0, [&] {
        return static_cast<jint>(NativeFrameGeneration::instance().presentPending(
            jni::as<VkSwapchainKHR>(swapchain), swapchainImagesFrom(env, swapchainImages), jni::as<VkQueue>(queue),
            static_cast<uint32_t>(swapchainWidth), static_cast<uint32_t>(swapchainHeight)));
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_FrameGenerationProxy_waitPresentIdle(JNIEnv *,
                                                                                                        jclass) {
    jni::guard("[Frame Generation] waitPresentIdle", [] { NativeFrameGeneration::instance().waitPresentIdle(); });
}

} // extern "C"
