#include "prism/all_extern.hpp"
#include "prism/middleware/jni_support.hpp"
#include "com_g2806_radiante_client_proxy_vulkan_PhotoProxy.h"
#include "prism/render/modules/world/tone_mapping/tone_mapping_module.hpp"
#include "prism/render/photo_mode.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"

#include <algorithm>
#include <mutex>
#include <string>

namespace jni = prism::jni;

extern "C" {

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_PhotoProxy_setActive(JNIEnv *,
                                                                                        jclass,
                                                                                        jboolean active) {
    jni::guard("[Radiante] PhotoProxy.setActive", [active] { PhotoMode::instance().setActive(jni::isTrue(active)); });
}

JNIEXPORT jboolean JNICALL Java_com_g2806_radiante_client_proxy_vulkan_PhotoProxy_isActive(JNIEnv *, jclass) {
    return jni::boolean(jni::guard("[Radiante] PhotoProxy.isActive", false, [] { return PhotoMode::instance().active(); }));
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_PhotoProxy_setLens(
    JNIEnv *, jclass, jfloat apertureRadius, jfloat focusDistance, jint blades, jfloat bladeRotation) {
    jni::guard("[Radiante] PhotoProxy.setLens", [=] {
        PhotoMode::instance().setLens(apertureRadius, focusDistance, static_cast<int>(blades), bladeRotation);
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_PhotoProxy_setTargetSamples(JNIEnv *,
                                                                                               jclass,
                                                                                               jint targetSamples) {
    jni::guard("[Radiante] PhotoProxy.setTargetSamples", [targetSamples] {
        PhotoMode::instance().setTargetSamples(static_cast<uint32_t>(std::max<jint>(1, targetSamples)));
    });
}

JNIEXPORT jint JNICALL Java_com_g2806_radiante_client_proxy_vulkan_PhotoProxy_getTargetSamples(JNIEnv *, jclass) {
    return static_cast<jint>(
        jni::guard("[Radiante] PhotoProxy.getTargetSamples", 256u, [] { return PhotoMode::instance().targetSamples(); }));
}

JNIEXPORT jint JNICALL Java_com_g2806_radiante_client_proxy_vulkan_PhotoProxy_getSampleCount(JNIEnv *, jclass) {
    return static_cast<jint>(
        jni::guard("[Radiante] PhotoProxy.getSampleCount", 0u, [] { return PhotoMode::instance().sampleCount(); }));
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_PhotoProxy_resetAccumulation(JNIEnv *, jclass) {
    jni::guard("[Radiante] PhotoProxy.resetAccumulation", [] { PhotoMode::instance().resetAccumulation(); });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_PhotoProxy_setExposureBias(JNIEnv *,
                                                                                              jclass,
                                                                                              jfloat exposureBias) {
    jni::guard("[Radiante] PhotoProxy.setExposureBias", [exposureBias] {
        PhotoMode::instance().setExposureOverride(true, exposureBias);
        auto framework = jni::framework();
        if (framework == nullptr) return;
        std::scoped_lock lock(framework->recreateMtx());
        if (framework->lastRebuildFailed()) return;
        auto pipeline = framework->pipeline();
        auto worldPipeline = pipeline != nullptr ? pipeline->worldPipeline() : nullptr;
        if (worldPipeline != nullptr && worldPipeline->isPhotoMode()) {
            worldPipeline->setRuntimeAttribute(ToneMappingModule::NAME,
                                               "render_pipeline.module.tone_mapping.attribute.exposure_bias",
                                               std::to_string(exposureBias));
        }
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_proxy_vulkan_PhotoProxy_requestCapture(
    JNIEnv *env, jclass, jstring pngPath, jstring exrPath, jboolean waitForTarget) {
    jni::guard("[Radiante] PhotoProxy.requestCapture", [&] {
        const std::u16string png = jni::utf16(env, pngPath);
        const std::u16string exr = jni::utf16(env, exrPath);
        PhotoMode::instance().requestCapture(png.empty() ? std::filesystem::path{} : std::filesystem::path(png),
                                             exr.empty() ? std::filesystem::path{} : std::filesystem::path(exr),
                                             jni::isTrue(waitForTarget));
    });
}

JNIEXPORT jint JNICALL Java_com_g2806_radiante_client_proxy_vulkan_PhotoProxy_pollCaptureStatus(JNIEnv *env,
                                                                                                jclass,
                                                                                                jobjectArray outMessage) {
    std::u16string message;
    const int status = jni::guard("[Radiante] PhotoProxy.pollCaptureStatus", 0,
                                  [&] { return PhotoMode::instance().pollCaptureStatus(message); });
    if (outMessage != nullptr && env->GetArrayLength(outMessage) > 0) {
        jstring text = env->NewString(reinterpret_cast<const jchar *>(message.data()),
                                      static_cast<jsize>(message.size()));
        env->SetObjectArrayElement(outMessage, 0, text);
        env->DeleteLocalRef(text);
    }
    return static_cast<jint>(status);
}

} // extern "C"
