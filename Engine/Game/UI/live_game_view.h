#pragma once
#include "game_view.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/client_source_spawn.h"
#include <cmath>

namespace dingosdk {
// Native camera movement follows the client tick. World overlays must use
// the final camera at draw time, rather than projecting through that tick's
// earlier matrix. Never follow a recycled object of an unrelated type.
inline bool refresh_game_view(std::uintptr_t base, GameView& view) noexcept {
    if (!base || !view.camera || (view.camera & 7)) return false;
    std::uintptr_t vtable{};
    if (!memory::peek(view.camera, vtable) ||
        (vtable != base + game::build::v20260929::engine::camera_vtable &&
         vtable != base + game::build::v20260929::client_source_spawn::free_camera_vtable)) return false;
    std::array<float, 16> world{};
    float fov{};
    if (!memory::peek(view.camera + 0x50, world) || !memory::peek(view.camera + 0xac, fov)) return false;
    if (!std::isfinite(fov) || fov <= 1 || fov >= 175) return false;
    // Fourth lanes are SIMD metadata. Match the native camera validation
    // used by Freecam and first person: validate coordinates and basis only.
    for (std::size_t row=0; row<4; ++row)
        for (std::size_t axis=0; axis<3; ++axis)
            if (!std::isfinite(world[row*4+axis]) || std::abs(world[row*4+axis]) > 1e6f) return false;
    for (std::size_t row=0; row<3; ++row) {
        float norm{};
        for (std::size_t axis=0; axis<3; ++axis) norm += world[row*4+axis]*world[row*4+axis];
        if (std::abs(norm-1) > .05f) return false;
        for (std::size_t other=row+1; other<3; ++other) {
            float dot{};
            for (std::size_t axis=0; axis<3; ++axis) dot += world[row*4+axis]*world[other*4+axis];
            if (std::abs(dot) > .05f) return false;
        }
    }
    view.world = world;
    view.vertical_fov = fov;
    return true;
}
} // namespace dingosdk
