#include "prism/render/hand_primary_bounds.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <random>
#include <stdexcept>

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    using namespace hand_primary;
    std::mt19937 rng(263);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    // Check projected points throughout animated/transformed boxes, with wide and narrow hand/world FOV ratios,
    // portrait through ultrawide views, and exact hand-FOV scaling. No triangle interior may escape its envelope.
    size_t checked = 0;
    for (float aspect : {0.5f, 1.0f, 16.0f/9.0f, 32.0f/9.0f}) {
        const glm::mat4 projection = glm::perspective(glm::radians(100.0f), aspect, 0.05f, 1000.0f);
        for (float scale : {0.3f, 1.0f, 1.00001f, 2.0f, 0.0f}) {
            const float effective = scale > 0 && std::abs(scale-1) > 1e-4f ? scale : 1;
            for (int frame = 0; frame < 100; ++frame) {
                auto transform = glm::translate(glm::mat4(1), glm::vec3(unit(rng)-0.5f, unit(rng)-0.5f, -2.0f));
                transform = glm::rotate(transform, unit(rng)*6.28f, glm::normalize(glm::vec3(1,2,3)));
                const glm::vec3 lo(-0.4f), hi(0.4f);
                auto b = project(lo, hi, transform, projection, scale);
                for (int i=0;i<100;++i) {
                    glm::vec4 v = transform * glm::vec4(glm::mix(lo,hi,glm::vec3(unit(rng),unit(rng),unit(rng))),1);
                    v.x /= effective; v.y /= effective;
                    auto clip = projection*v;
                    auto ndc = glm::vec2(clip)/clip.w;
                    require(ndc.x>=b.x && ndc.y>=b.y && ndc.x<=b.z && ndc.y<=b.w, "projected geometry escaped bounds");
                    ++checked;
                }
            }
        }
    }
    // Radiante can generate rays toward positive view z: depth-zero unprojection, rather than clip-w sign,
    // determines the visible half-space. Exercise that convention as well as ordinary positive-w projections.
    const auto reversedNear = glm::perspective(glm::radians(70.0f), 2.0f, -0.05f, 1000.0f);
    const glm::vec3 positiveLo(-0.2f,-0.4f,0.3f), positiveHi(0.4f,0.1f,1.1f);
    const auto positiveBounds = project(positiveLo,positiveHi,glm::mat4(1),reversedNear,0.6f);
    require(positiveBounds != fullScreen(), "negative-w forward geometry must produce useful bounds");
    for (int i=0;i<1000;++i) {
        glm::vec4 v(glm::mix(positiveLo,positiveHi,glm::vec3(unit(rng),unit(rng),unit(rng))),1);
        v.x/=0.6f;v.y/=0.6f;
        const auto clip=reversedNear*v;const auto ndc=glm::vec2(clip)/clip.w;
        require(ndc.x>=positiveBounds.x && ndc.y>=positiveBounds.y && ndc.x<=positiveBounds.z && ndc.y<=positiveBounds.w,
                "negative-w geometry escaped bounds");
    }
    checked += 1000;
    const auto p = glm::perspective(glm::radians(70.0f), 2.0f, 0.05f, 1000.0f);
    require(project(glm::vec3(-1),glm::vec3(1),glm::mat4(1),p,1)==fullScreen(), "eye-plane fallback");
    require(project(glm::vec3(1),glm::vec3(-1),glm::mat4(1),p,1)==fullScreen(), "invalid bounds fallback");
    require(project(glm::vec3(-1),glm::vec3(1),glm::mat4(1),p,NAN)==fullScreen(), "invalid scale fallback");
    require(project(glm::vec3(NAN),glm::vec3(1),glm::mat4(1),p,1)==fullScreen(), "nonfinite geometry fallback");
    require(project(glm::vec3(-1),glm::vec3(1),glm::mat4(1),glm::mat4(1),1)==fullScreen(), "projection fallback");
    std::cout << checked << " projected points enclosed; fallback checks passed\n";
}
