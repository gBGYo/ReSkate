#pragma once
#include "slam_model.h"
#include <cstring>
#include <optional>
#include <span>

namespace dingosdk::slam {
// Main joint names from the standard skeleton, checked against the live
// physics body map on 2026-10-03. Ground (380) is the unscored dummy slot.
inline std::optional<Region> region_for_joint(int joint) noexcept {
    switch (joint) {
    case 101: case 102: case 103: return Region::head;
    case 0: case 1: case 7: case 42: case 43: case 44: case 45: case 380: return Region::torso;
    case 46: case 47: case 48: case 49: return Region::right_arm;
    case 275: case 276: case 277: case 278: return Region::left_arm;
    case 8: case 9: case 10: case 11: return Region::right_leg;
    case 341: case 342: case 343: case 344: return Region::left_leg;
    default: return std::nullopt;
    }
}
inline Vec3 model_to_world(const std::array<float, 16>& actor, const Vec3& local) noexcept {
    Vec3 world{};
    for (std::size_t a = 0; a < 3; ++a)
        world[a] = actor[12+a] + local[0]*actor[a] + local[1]*actor[4+a] + local[2]*actor[8+a];
    return world;
}
struct PhysicsPoseHeader { std::uintptr_t model{}, local{}; std::uint32_t count{}; };
// Captured native header: two matrix buffers, flags, count at 1c, float dt
// at 20. Count is not at 20; that is a common but unsafe layout assumption.
inline std::optional<PhysicsPoseHeader> decode_physics_pose_header(std::span<const unsigned char> bytes) noexcept {
    if (bytes.size() < 0x20) return std::nullopt;
    PhysicsPoseHeader out;
    std::memcpy(&out.model, bytes.data(), 8);
    std::memcpy(&out.local, bytes.data()+8, 8);
    std::memcpy(&out.count, bytes.data()+0x1c, 4);
    constexpr auto highest = std::uintptr_t{0x00007fffffffffff};
    if (out.count != 395 || out.model < 0x10000 || out.local < 0x10000 ||
        out.model > highest - out.count*0x40ULL || out.local > highest - out.count*0x40ULL) return std::nullopt;
    return out;
}
}
