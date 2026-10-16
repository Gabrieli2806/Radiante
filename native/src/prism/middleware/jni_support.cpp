#include "prism/middleware/jni_support.hpp"

#include "prism/render/renderer.hpp"
#include "prism/render/world.hpp"

namespace prism::jni {

std::optional<std::string> utf8(JNIEnv *env, jstring value) {
    if (value == nullptr) return std::nullopt;
    // Encoded straight into the result, where GetStringUTFChars has the JVM allocate a copy of its own first.
    std::string text(static_cast<size_t>(env->GetStringUTFLength(value)), '\0');
    env->GetStringUTFRegion(value, 0, env->GetStringLength(value), text.data());
    return text;
}

std::u16string utf16(JNIEnv *env, jstring value) {
    if (value == nullptr) return {};
    static_assert(sizeof(jchar) == sizeof(char16_t));
    std::u16string text(static_cast<size_t>(env->GetStringLength(value)), u'\0');
    env->GetStringRegion(value, 0, static_cast<jsize>(text.size()), reinterpret_cast<jchar *>(text.data()));
    return text;
}

std::vector<std::string> utf8Array(JNIEnv *env, jobjectArray values) {
    std::vector<std::string> texts;
    if (values == nullptr) return texts;
    const jsize count = env->GetArrayLength(values);
    texts.reserve(static_cast<size_t>(count));
    for (jsize i = 0; i < count; i++) {
        auto element = static_cast<jstring>(env->GetObjectArrayElement(values, i));
        texts.push_back(utf8(env, element).value_or(std::string()));
        // Each element is a new local reference; a long array would otherwise fill the JVM's table of them.
        if (element != nullptr) env->DeleteLocalRef(element);
    }
    return texts;
}

std::shared_ptr<Framework> framework() {
    Renderer *renderer = Renderer::try_instance();
    return renderer != nullptr ? renderer->framework() : nullptr;
}

std::shared_ptr<Textures> textures() {
    Renderer *renderer = Renderer::try_instance();
    return renderer != nullptr ? renderer->textures() : nullptr;
}

std::shared_ptr<Buffers> buffers() {
    Renderer *renderer = Renderer::try_instance();
    return renderer != nullptr ? renderer->buffers() : nullptr;
}

std::shared_ptr<World> world() {
    Renderer *renderer = Renderer::try_instance();
    return renderer != nullptr ? renderer->world() : nullptr;
}

std::shared_ptr<Chunks> chunks() {
    auto current = world();
    return current != nullptr ? current->chunks() : nullptr;
}

} // namespace prism::jni
