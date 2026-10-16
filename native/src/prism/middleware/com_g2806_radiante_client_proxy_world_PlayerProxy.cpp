#include "prism/all_extern.hpp"
#include "prism/middleware/jni_support.hpp"
#include "prism/render/world.hpp"

namespace jni = prism::jni;

extern "C" {

JNIEXPORT void JNICALL
Java_com_g2806_radiante_client_proxy_world_PlayerProxy_setCameraPos(JNIEnv *, jclass, jdouble x, jdouble y, jdouble z) {
    jni::guard("[Radiante] setCameraPos", [=] {
        if (auto world = jni::world()) world->setCameraPos(glm::dvec3{x, y, z});
    });
}

} // extern "C"
