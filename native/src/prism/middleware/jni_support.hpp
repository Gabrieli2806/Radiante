#pragma once

#include <jni.h>

#include "prism/util/logging.hpp"

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <type_traits>
#include <vector>

class Buffers;
class Chunks;
class Framework;
class Textures;
class World;

// What the JNI entry points next to this file share. Java passes native memory and Vulkan handles as longs. And no
// C++ exception may unwind into the JVM, which cannot handle one and takes the game down with it: the entry points
// run their work through guard().
namespace prism::jni {

// The native address or Vulkan handle a Java long carries.
template <typename T>
    requires std::is_pointer_v<T>
T as(jlong value) noexcept {
    return reinterpret_cast<T>(static_cast<std::intptr_t>(value));
}

// A native address or Vulkan handle as the long Java keeps it in.
template <typename T>
    requires std::is_pointer_v<T>
jlong toLong(T value) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::intptr_t>(value));
}

inline bool isTrue(jboolean value) noexcept {
    return value != JNI_FALSE;
}

inline jboolean boolean(bool value) noexcept {
    return value ? JNI_TRUE : JNI_FALSE;
}

// A Java string's text in JNI's modified UTF-8, the form names and values cross in; nothing for null.
std::optional<std::string> utf8(JNIEnv *env, jstring value);

// A Java string's UTF-16 text, empty for null: a file path keeps every character this way.
std::u16string utf16(JNIEnv *env, jstring value);

// The texts of a String[], "" for a null element; none for a null array.
std::vector<std::string> utf8Array(JNIEnv *env, jobjectArray values);

// The renderer's parts: null before initRenderer, and after close, which drops them but keeps the renderer.
std::shared_ptr<Framework> framework();
std::shared_ptr<Textures> textures();
std::shared_ptr<Buffers> buffers();
std::shared_ptr<World> world();
std::shared_ptr<Chunks> chunks();

// The message of the exception being handled, or null when it is not a std::exception. Only for a catch block: the
// text lives as long as the exception does.
inline const char *currentReason() noexcept {
    try {
        throw;
    } catch (const std::exception &e) {
        return e.what();
    } catch (...) {
        return nullptr;
    }
}

// Writes "<subject> failed: <reason>" for the exception being handled to the error log, which reaches the game's log.
// The subject is text, or a callable that writes it to the stream when it is made from the call's arguments, so that
// building it costs nothing until something fails.
template <typename Subject>
void reportFailure(const Subject &subject) noexcept {
    std::ostream &log = radiante::err();
    if constexpr (std::is_invocable_v<const Subject &, std::ostream &>) {
        subject(log);
    } else {
        log << subject;
    }
    if (const char *reason = currentReason(); reason != nullptr) {
        log << " failed: " << reason << std::endl;
    } else {
        log << " failed with unknown exception" << std::endl;
    }
}

// Runs body() and returns its result; when it throws, reports the exception and returns `fallback` instead.
template <typename Subject, typename Body, typename Result = std::invoke_result_t<Body &>>
Result guard(const Subject &subject, std::type_identity_t<Result> fallback, Body &&body) noexcept {
    try {
        return body();
    } catch (...) {
        reportFailure(subject);
    }
    return fallback;
}

// The same for work with no result.
template <typename Subject, typename Body>
void guard(const Subject &subject, Body &&body) noexcept {
    try {
        body();
    } catch (...) {
        reportFailure(subject);
    }
}

} // namespace prism::jni
