#include "prism/all_extern.hpp"
#include "prism/middleware/jni_support.hpp"
// The header javac writes for the Java class: a native method whose signature here differs from the Java one
// then fails to compile instead of failing in the game.
#include "com_g2806_radiante_client_proxy_vulkan_HdrProxy.h"
#include "prism/render/hdr_output.hpp"

#include <cstdint>

namespace jni = prism::jni;

extern "C" {

JNIEXPORT jboolean JNICALL Java_com_g2806_radiante_client_proxy_vulkan_HdrProxy_compose(JNIEnv *,
                                                                                        jclass,
                                                                                        jlong commandBuffer,
                                                                                        jlong minecraftImage,
                                                                                        jint minecraftLayout,
                                                                                        jint minecraftFormat,
                                                                                        jint width,
                                                                                        jint height,
                                                                                        jlong swapchainImage,
                                                                                        jint swapchainWidth,
                                                                                        jint swapchainHeight,
                                                                                        jfloat paperWhiteNits,
                                                                                        jfloat peakNits,
                                                                                        jboolean flipY,
                                                                                        jboolean debugView) {
    return jni::boolean(jni::guard("[HDR] compose", false, [&] {
        return HdrOutput::instance().compose(
            jni::as<VkCommandBuffer>(commandBuffer), jni::as<VkImage>(minecraftImage),
            static_cast<VkImageLayout>(minecraftLayout), static_cast<VkFormat>(minecraftFormat),
            static_cast<uint32_t>(width), static_cast<uint32_t>(height), jni::as<VkImage>(swapchainImage),
            static_cast<uint32_t>(swapchainWidth), static_cast<uint32_t>(swapchainHeight), paperWhiteNits, peakNits,
            jni::isTrue(flipY), jni::isTrue(debugView));
    }));
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_HdrProxy_setTarget(JNIEnv *,
                                                                                      jclass,
                                                                                      jboolean active,
                                                                                      jfloat paperWhiteNits,
                                                                                      jfloat peakNits) {
    jni::guard("[HDR] setTarget", [&] {
        HdrOutput::instance().setTarget(jni::isTrue(active), paperWhiteNits, peakNits);
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_HdrProxy_capturePreview(JNIEnv *env,
                                                                                           jclass,
                                                                                           jlong commandBuffer,
                                                                                           jlong minecraftImage,
                                                                                           jint minecraftLayout,
                                                                                           jint minecraftFormat,
                                                                                           jint width,
                                                                                           jint height,
                                                                                           jfloat paperWhiteNits,
                                                                                           jfloat peakNits,
                                                                                           jboolean flipY,
                                                                                           jboolean debugView,
                                                                                           jstring path) {
    jni::guard("[HDR] capturePreview", [&] {
        HdrOutput::instance().capturePreview(
            jni::as<VkCommandBuffer>(commandBuffer), jni::as<VkImage>(minecraftImage),
            static_cast<VkImageLayout>(minecraftLayout), static_cast<VkFormat>(minecraftFormat),
            static_cast<uint32_t>(width), static_cast<uint32_t>(height), paperWhiteNits, peakNits, jni::isTrue(flipY),
            jni::isTrue(debugView), jni::utf8(env, path).value_or(""));
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_HdrProxy_setCalibration(JNIEnv *,
                                                                                           jclass,
                                                                                           jint mode,
                                                                                           jfloat ax,
                                                                                           jfloat ay,
                                                                                           jfloat aw,
                                                                                           jfloat ah,
                                                                                           jfloat bx,
                                                                                           jfloat by,
                                                                                           jfloat bw,
                                                                                           jfloat bh) {
    jni::guard("[HDR] setCalibration", [&] {
        HdrOutput::instance().setCalibration(mode, glm::vec4(ax, ay, aw, ah), glm::vec4(bx, by, bw, bh));
    });
}

} // extern "C"
