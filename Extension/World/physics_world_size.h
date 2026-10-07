#pragma once

#include <cstdint>
#include <string>

namespace dingosdk {
// Static collision slots per physics world. Stock worlds hold 8,200 bodies
// (+1 internal); a large custom map gives every collision part its own body.
// Kept below 65,536 so no 16-bit index elsewhere in the engine can wrap.
inline constexpr std::uint32_t physics_world_body_target = 65000;
// Pools that each static mesh body also draws from scale with BodyCount, up to this.
inline constexpr std::uint32_t physics_world_pool_ceiling = 1u << 20;

// Install after Detours hook service init, before any level loads. Raises the
// body pool and its companion static-collision pools at world creation only;
// queries, contacts, joints and per-query result limits keep their native size.
bool start_physics_world_size(std::uintptr_t base, std::string& error);
} // namespace dingosdk
