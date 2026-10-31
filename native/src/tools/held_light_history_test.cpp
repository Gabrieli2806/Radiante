#include "prism/render/held_light_history.hpp"

#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    gpu::Data::WorldUBO dark{};
    auto torch = dark;
    torch.heldLightPos = glm::vec4(1, 2, 3, 40);
    torch.heldLightColor = glm::vec4(1, 0.5, 0.1, 1);
    torch.heldLightBrightness = 1;
    require(!held_light_history::changed(dark, dark), "idle frames reset");
    require(held_light_history::changed(torch, dark), "equipping a light kept stale history");
    require(held_light_history::changed(dark, torch), "removing a light kept stale history");
    require(!held_light_history::changed(torch, torch), "steady light reset");
    auto moved = torch;
    moved.heldLightPos.x += 2;
    moved.cameraPos.x += 3;
    require(!held_light_history::changed(moved, torch), "camera/hand movement reset");
    auto changed = torch;
    changed.heldLightColor.g = 0.8;
    require(held_light_history::changed(changed, torch), "source colour change ignored");
    changed = torch;
    changed.heldLightBrightness = 0;
    require(held_light_history::changed(changed, torch), "brightness off ignored");
    changed = torch;
    changed.heldLightBrightness = 1.01;
    require(held_light_history::changed(changed, torch), "one percent adjustment ignored");
    changed = torch;
    changed.heldLightColor.g += 1e-7f;
    require(!held_light_history::changed(changed, torch), "rounding noise reset");
    auto offHand = torch;
    offHand.offHandLightPos = torch.heldLightPos;
    offHand.offHandLightColor = torch.heldLightColor;
    require(held_light_history::changed(offHand, torch), "off-hand light ignored");
    require(held_light_history::changed(torch, offHand), "off-hand removal ignored");
    auto inactive = dark;
    inactive.heldLightColor = glm::vec4(1, 1, 1, 0);
    inactive.heldLightPos = torch.heldLightPos;
    inactive.heldLightBrightness = 4;
    require(!held_light_history::changed(inactive, dark), "inactive colour metadata reset");
    torch.heldLightBrightness = 0;
    changed = torch;
    changed.heldLightColor = glm::vec4(0.2, 0.5, 1, 1);
    require(!held_light_history::changed(changed, torch), "zero-brightness item swaps reset");
    uint32_t remaining = 0;
    require(held_light_history::resetFrame(true, 3, remaining), "transition did not reset");
    require(held_light_history::resetFrame(false, 3, remaining), "second in-flight slot was retained");
    require(held_light_history::resetFrame(false, 3, remaining), "third in-flight slot was retained");
    require(!held_light_history::resetFrame(false, 3, remaining), "stable frames never resumed accumulation");
    require(held_light_history::resetFrame(true, 0, remaining), "empty startup window missed reset");
    require(!held_light_history::resetFrame(false, 0, remaining), "single startup reset did not expire");
    std::cout << "Held-light history: equip, removal, both hands, colour, brightness, motion and idle passed\n";
}
