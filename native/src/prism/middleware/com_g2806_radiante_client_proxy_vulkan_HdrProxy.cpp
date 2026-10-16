#include "prism/all_extern.hpp"
#include "prism/middleware/jni_support.hpp"
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

} // extern "C"
