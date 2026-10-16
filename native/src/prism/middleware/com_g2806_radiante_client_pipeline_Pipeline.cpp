#include "prism/all_extern.hpp"
#include "prism/middleware/jni_support.hpp"
#include "prism/render/buffers.hpp"
#include "prism/render/modules/world/ray_tracing/ray_tracing_module.hpp"
#include "prism/render/modules/world/shader_pack/shader_pack.hpp"
#include "prism/render/pipeline.hpp"
#include "prism/render/render_framework.hpp"

#include <cstdint>
#include <mutex>
#include <ostream>
#include <string>
#include <utility>

namespace jni = prism::jni;

namespace {

// The module an attribute belongs to, by the names the modules give theirs: render_pipeline.module.<m>.attribute.<a>
// is an attribute of render_pipeline.module.<m>.name. Empty for a name that does not follow that pattern.
std::string moduleOf(const std::string &attribute) {
    const size_t at = attribute.find(".attribute.");
    return at == std::string::npos ? std::string() : attribute.substr(0, at) + ".name";
}

} // namespace

extern "C" {

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_pipeline_Pipeline_buildNative(JNIEnv *, jclass, jlong params) {
    jni::guard("[Radiante] buildNative", [params] {
        auto framework = jni::framework();
        auto pipeline = framework != nullptr ? framework->pipeline() : nullptr;
        if (pipeline != nullptr) pipeline->buildWorldPipelineBlueprint(jni::as<WorldPipelineBuildParams *>(params));
    });
}

JNIEXPORT void JNICALL Java_com_g2806_radiante_client_pipeline_Pipeline_collectNativeModules(JNIEnv *, jclass) {
    jni::guard("[Radiante] collectNativeModules", [] { Pipeline::collectWorldModules(); });
}

JNIEXPORT jboolean JNICALL Java_com_g2806_radiante_client_pipeline_Pipeline_isNativeModuleAvailable(JNIEnv *env,
                                                                                                    jclass,
                                                                                                    jstring name) {
    return jni::boolean(jni::guard("[Radiante] isNativeModuleAvailable", false, [&] {
        auto module = jni::utf8(env, name);
        return module.has_value() && Pipeline::worldModuleConstructors.contains(*module);
    }));
}

// The attributes a module offers and their translations, as JSON for the settings screen; "{}" for a module without
// any. Only the ray tracing module has some: those of the shader pack the attributes point at.
JNIEXPORT jstring JNICALL Java_com_g2806_radiante_client_pipeline_Pipeline_getAttributes(
    JNIEnv *env, jclass, jstring name, jobjectArray attributes, jstring language) {
    const std::string json = jni::guard("[Radiante] getAttributes", std::string("{}"), [&] {
        auto module = jni::utf8(env, name);
        if (!module.has_value() || *module != RayTracingModule::NAME) return std::string("{}");
        RayTracingModule rayTracing;
        return rayTracing.getAttributes(jni::utf8Array(env, attributes), jni::utf8(env, language).value_or("en_us"));
    });
    return env->NewStringUTF(json.c_str());
}

// Applies an attribute without rebuilding anything: a shader pack attribute the pack marks "runtime" (the shaders read
// it from the world uniform from the next frame on), or one a built module takes live (tone mapping, NRD, the post
// render stars). False (an unknown attribute, one only a build applies, a value that does not read, or nothing built
// yet) tells Java to rebuild the pipeline for it instead.
JNIEXPORT jboolean JNICALL Java_com_g2806_radiante_client_pipeline_Pipeline_setRuntimeAttribute(JNIEnv *env,
                                                                                                jclass,
                                                                                                jstring name,
                                                                                                jstring value) {
    std::string attribute;
    const auto subject = [&attribute](std::ostream &out) { out << "[Radiante] setting " << attribute << " live"; };
    return jni::boolean(jni::guard(subject, false, [&] {
        auto nameText = jni::utf8(env, name);
        auto valueText = jni::utf8(env, value);
        if (!nameText.has_value() || !valueText.has_value()) return false;
        attribute = std::move(*nameText);

        auto framework = jni::framework();
        auto buffers = jni::buffers();
        if (framework == nullptr || buffers == nullptr) return false;

        uint32_t slot = 0;
        glm::vec4 packed(0.0f);
        {
            // Frames and rebuilds run under this lock, so the change falls between two frames; a frame also decides
            // the pack's execution conditions from the values set here.
            std::scoped_lock lock(framework->recreateMtx());
            // A failed rebuild leaves the pipeline half made, and only a full build repairs it.
            if (framework->lastRebuildFailed()) return false;
            auto pipeline = framework->pipeline();
            auto worldPipeline = pipeline != nullptr ? pipeline->worldPipeline() : nullptr;
            auto blueprint = pipeline != nullptr ? pipeline->worldPipelineBlueprint() : nullptr;
            if (worldPipeline == nullptr || blueprint == nullptr) return false;

            // Rebuilds made from the blueprint alone (a resize, vsync) build the modules again: they keep the value
            // only when it is recorded there too.
            const std::string module = moduleOf(attribute);
            if (module != RayTracingModule::NAME && worldPipeline->setRuntimeAttribute(module, attribute, *valueText)) {
                blueprint->setAttributeValue(module, attribute, *valueText);
                return true;
            }
            // Anything else may be the shader pack's: a pack names its attributes as it likes.
            auto shaderPack = worldPipeline->shaderPack();
            if (shaderPack == nullptr) return false;
            if (!shaderPack->setRuntimeAttribute(attribute, *valueText, slot, packed)) return false;
            blueprint->setAttributeValue(RayTracingModule::NAME, attribute, *valueText);
        }
        // Not under the frame lock: the world uniform upload takes the buffers' lock and then the frame lock, and
        // taking the two the other way round here could deadlock against it.
        buffers->setPackParam(slot, packed);
        return true;
    }));
}

// Starts compiling, in the background, what the pipeline Java describes will need (the same block buildNative takes),
// while frames keep using the current one; Java builds it once the prewarm is done. False when nothing can be
// warmed (see Pipeline::prewarm): Java then builds straight away.
JNIEXPORT jboolean JNICALL Java_com_g2806_radiante_client_pipeline_Pipeline_prewarmNative(JNIEnv *,
                                                                                          jclass,
                                                                                          jlong params) {
    return jni::boolean(jni::guard("[Radiante] prewarmNative", false, [params] {
        auto framework = jni::framework();
        auto pipeline = framework != nullptr ? framework->pipeline() : nullptr;
        if (pipeline == nullptr) return false;
        // The passes are copied from the built module, which frames use: between two frames.
        std::scoped_lock lock(framework->recreateMtx());
        return pipeline->prewarm(jni::as<WorldPipelineBuildParams *>(params));
    }));
}

// The newest prewarm's state and counts into out: state (0 idle, 1 running, 2 done, 3 failed, 4 cancelled), shaders
// compiled, shaders to compile, pipelines built, pipelines to build. Takes no frame lock: Java polls it every tick.
JNIEXPORT void JNICALL Java_com_g2806_radiante_client_pipeline_Pipeline_prewarmStatus(JNIEnv *env,
                                                                                      jclass,
                                                                                      jintArray out) {
    jni::guard("[Radiante] prewarmStatus", [&] {
        if (out == nullptr || env->GetArrayLength(out) < 5) return;
        auto framework = jni::framework();
        auto pipeline = framework != nullptr ? framework->pipeline() : nullptr;
        const ShaderPrewarm::Status status =
            pipeline != nullptr ? pipeline->shaderPrewarm().status() : ShaderPrewarm::Status{};
        const jint values[5] = {
            static_cast<jint>(status.state),         static_cast<jint>(status.shadersDone),
            static_cast<jint>(status.shadersTotal),  static_cast<jint>(status.pipelinesDone),
            static_cast<jint>(status.pipelinesTotal),
        };
        env->SetIntArrayRegion(out, 0, 5, values);
    });
}

} // extern "C"
