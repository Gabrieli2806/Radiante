#pragma once

#include <cstdint>
#include <functional>

// Deferred destruction of GPU objects.
//
// A Vulkan object may only be destroyed once no submitted work refers to it any more. Frames are recorded while the
// previous ones still run, so an object dropped while recording frame N can still be in use by frames N-1 .. N-k.
// Instead of every owner keeping its old objects alive by hand, destruction goes through here: what is retired while
// a frame slot is open is destroyed when that slot comes round again, after the framework has waited for the work it
// held. Safe to call from any thread.
namespace gpu::retire {

// How many frame slots there are (the frames that can be in flight at once). Called again when it changes.
void configure(uint32_t frameSlots);

// Runs `destroy` once no frame that may use what it destroys is in flight any more.
void later(std::function<void()> destroy);

// Called when a frame slot opens, after waiting for the GPU work it last held: runs what was retired in that slot.
void openSlot(uint32_t slot);

// Runs everything still waiting. Only once the device is idle (rebuilds that wait for it, shutdown).
void drain();

} // namespace gpu::retire
