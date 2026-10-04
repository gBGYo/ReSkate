#pragma once
#include <cstdint>

namespace dingosdk::replay_export {
// The replay editor steps the replay by one frame of video for every frame the
// game renders, but the engine encodes at most one frame per world update and
// drops the rest. Rendering faster than it updates (an uncapped frame rate),
// an export came out with a fraction of its frames and played that much too
// fast. Every rendered frame of a replay export is encoded instead.
// Replay X-rays are rendered into a private target and composited into the
// captured BGRA frame before encoding, independently of display presentation.
// Each native capture slot retains its own pose, camera and injury snapshot
// from the texture copy, through any readback/encoding delay.
bool start(std::uintptr_t base) noexcept;
}
