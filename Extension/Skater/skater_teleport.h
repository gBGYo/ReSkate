#pragma once
#include "no_bail.h"
#include <array>
#include <cmath>

namespace dingosdk {
// Affine, orthonormal and right-handed; no scale/shear or invented facing.
inline bool valid_skater_teleport_transform(const std::array<float,16>& pose) noexcept {
    for (float value:pose) if (!std::isfinite(value) || std::abs(value)>1000000) return false;
    if (std::abs(pose[3])>.001f || std::abs(pose[7])>.001f || std::abs(pose[11])>.001f || std::abs(pose[15]-1)>.001f) return false;
    for (unsigned a=0;a<3;++a) for (unsigned b=a;b<3;++b) {
        float dot{}; for (unsigned axis=0;axis<3;++axis) dot+=pose[a*4+axis]*pose[b*4+axis];
        if (std::abs(dot-(a==b ? 1.f : 0.f))>.02f) return false;
    }
    const float determinant=pose[0]*(pose[5]*pose[10]-pose[6]*pose[9])-
        pose[1]*(pose[4]*pose[10]-pose[6]*pose[8])+pose[2]*(pose[4]*pose[9]-pose[5]*pose[8]);
    return determinant>.98f && determinant<1.02f;
}
enum class SkaterTeleportState { unavailable, busy, submitted, idle, interrupted };
struct SkaterTeleportReceipt {
    std::uintptr_t base{}, manager{};
    std::uint32_t serial{};
};
struct SkaterTeleportSubmission {
    SkaterTeleportState state=SkaterTeleportState::unavailable;
    SkaterTeleportReceipt receipt;
};
// Immediate, owned dispatch on the recorded game-update thread. No native
// request remains queued in the SDK after a map change or canceled retry.
SkaterTeleportSubmission submit_owned_skater_teleport(std::uintptr_t client,const LocalBailOwner& owner,
    const std::array<float,16>& transform) noexcept;
// Read-only: idle means this same request has finished, not that it succeeded.
// The caller must verify the resulting owned actor pose and fresh physics.
SkaterTeleportState inspect_skater_teleport(const SkaterTeleportReceipt& receipt) noexcept;
}
