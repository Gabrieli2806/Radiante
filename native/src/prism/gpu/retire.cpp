#include "prism/gpu/retire.hpp"

#include <mutex>
#include <vector>

namespace gpu::retire {
namespace {

struct State {
    std::mutex mutex;
    // One list per frame slot: what was retired while that slot was the open one.
    std::vector<std::vector<std::function<void()>>> slots;
    // Retired before the first slot opened, or after a reconfiguration: run on the next drain.
    std::vector<std::function<void()>> orphans;
    uint32_t open = 0;
    bool configured = false;
};

// Never destroyed: at process exit the queued closures would release the device and allocator from a static
// destructor, after the driver may already be gone.
State &state() {
    static State *s = new State();
    return *s;
}

void runAll(std::vector<std::function<void()>> &list) {
    for (auto &destroy : list) {
        if (destroy) destroy();
    }
    list.clear();
}

} // namespace

void configure(uint32_t frameSlots) {
    {
        State &s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        for (auto &slot : s.slots) {
            for (auto &destroy : slot) s.orphans.push_back(std::move(destroy));
        }
        s.slots.assign(frameSlots == 0 ? 1 : frameSlots, {});
        s.open = 0;
        s.configured = true;
    }
}

void later(std::function<void()> destroy) {
    if (!destroy) return;
    State &s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.configured) {
        s.orphans.push_back(std::move(destroy));
        return;
    }
    s.slots[s.open].push_back(std::move(destroy));
}

void openSlot(uint32_t slot) {
    std::vector<std::function<void()>> ready;
    {
        State &s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.configured) return;
        s.open = slot % static_cast<uint32_t>(s.slots.size());
        ready.swap(s.slots[s.open]);
    }
    // Destroyed outside the lock: a destructor may retire more objects.
    runAll(ready);
}

void drain() {
    for (;;) {
        std::vector<std::function<void()>> ready;
        {
            State &s = state();
            std::lock_guard<std::mutex> lock(s.mutex);
            ready.swap(s.orphans);
            for (auto &slot : s.slots) {
                for (auto &destroy : slot) ready.push_back(std::move(destroy));
                slot.clear();
            }
        }
        if (ready.empty()) return;
        runAll(ready);
    }
}

} // namespace gpu::retire
