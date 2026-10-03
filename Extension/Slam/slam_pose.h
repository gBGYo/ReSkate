#pragma once
#include "slam_telemetry.h"
#include <algorithm>
#include <cmath>

namespace dingosdk::slam {
using PoseMatrix = std::array<float,16>;
inline constexpr PoseMatrix identity_pose{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
// Parent-local animation transforms are scale.xyzw, Hamilton rotation.xyzw,
// position.xyzw. Fourth scale/position lanes contain metadata, not coordinates.
inline std::optional<PoseMatrix> append_pose(const PoseMatrix& parent, const std::array<float,12>& bone) noexcept {
    for (const auto i : {0u,1u,2u,4u,5u,6u,7u,8u,9u,10u})
        if (!std::isfinite(bone[i]) || std::abs(bone[i]) > 1000000) return std::nullopt;
    for (const auto i : {0u,1u,2u}) if (std::abs(bone[i]) > 20) return std::nullopt;
    const float x=bone[4], y=bone[5], z=bone[6], w=bone[7];
    const float norm=x*x+y*y+z*z+w*w;
    if (norm < .8f || norm > 1.2f) return std::nullopt;
    PoseMatrix local{
        (1-2*(y*y+z*z))*bone[0], 2*(x*y+z*w)*bone[0], 2*(x*z-y*w)*bone[0], 0,
        2*(x*y-z*w)*bone[1], (1-2*(x*x+z*z))*bone[1], 2*(y*z+x*w)*bone[1], 0,
        2*(x*z+y*w)*bone[2], 2*(y*z-x*w)*bone[2], (1-2*(x*x+y*y))*bone[2], 0,
        bone[8],bone[9],bone[10],1};
    PoseMatrix world{};
    for (std::size_t row=0; row<3; ++row)
        for (std::size_t axis=0; axis<3; ++axis)
            world[row*4+axis] = local[row*4]*parent[axis] + local[row*4+1]*parent[4+axis] + local[row*4+2]*parent[8+axis];
    const auto position = model_to_world(parent, {bone[8],bone[9],bone[10]});
    std::copy(position.begin(), position.end(), world.begin()+12);
    world[15]=1;
    return world;
}
// Named hierarchy from AnimBase_Default, also used by first-person's live
// head chain. AITrajectory already carries world placement: do not apply the
// entity transform a second time. Parents precede children in this subset.
inline constexpr std::array<int,26> render_joint_ids{0,1,7,42,43,44,45,101,102,103,46,47,48,49,275,276,277,278,8,9,10,11,341,342,343,344};
inline constexpr std::array<int,26> render_parent_indices{-1,0,1,2,3,4,5,6,7,8,6,10,11,12,6,14,15,16,2,18,19,20,2,22,23,24};
// Same limb ordering as the physics body map, with the dummy slot replaced
// by the actual Head joint. Physics-only markers never appear in the overlay.
inline constexpr std::array<int,24> overlay_joint_indices{9,8,7,17,16,15,14,13,12,11,10,6,5,4,3,25,24,23,22,21,20,19,18,2};
inline constexpr std::array<int,24> overlay_parents{1,2,11,4,5,6,11,8,9,10,11,12,13,14,23,16,17,18,23,20,21,22,23,-1};
}
