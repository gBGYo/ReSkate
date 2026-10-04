#pragma once
#include "slam_model.h"
#include <cmath>
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
// The captured body map has two head-region proxies: Neck (101) and Neck1
// (102), and no Head (103) body. Name the displayed anatomy explicitly rather
// than using a physics attachment joint as the injury/display identifier.
inline std::optional<int> injury_joint_for_body(int joint) noexcept {
    if (joint==102) return 103;
    if (joint==0 || joint==1 || joint==380 || !region_for_joint(joint)) return {};
    return joint;
}
inline std::optional<int> injury_joint_for_mesh(int joint) noexcept {
    if (joint==102) return 101; // Both neck mesh joints follow the neck proxy.
    if (joint==103) return 103; // Skull and its descendants follow the head proxy.
    return injury_joint_for_body(joint);
}
struct ContactDetail {Vec3 normal{}, point{}; float speed{}; bool grounded{}, point_valid{};};
// 4772d70 clears the complete 0x70 record every step, then writes normals
// at +00/+10 with matching relative normal-speed magnitudes at +50/+54.
// Both branches store the contact world position at +40. Choose the strongest
// usable direction, not simply the first unit-length vector in the record.
inline std::optional<ContactDetail> decode_contact_detail(std::span<const unsigned char> bytes) noexcept {
    if (bytes.size()<0x70) return {};
    std::optional<ContactDetail> best;
    bool grounded{};
    unsigned directions{};
    for (std::size_t slot=0;slot<2;++slot) {
        ContactDetail candidate;
        std::memcpy(candidate.normal.data(),bytes.data()+slot*0x10,sizeof(Vec3));
        std::memcpy(candidate.point.data(),bytes.data()+0x40,sizeof(Vec3));
        std::memcpy(&candidate.speed,bytes.data()+0x50+slot*4,sizeof(float));
        float norm{};
        bool valid=true;
        for (float lane : candidate.normal) {valid=valid && std::isfinite(lane); norm+=lane*lane;}
        if (!valid || norm<.25f || norm>2.25f) continue;
        ++directions;
        candidate.point_valid=true;
        for (float lane : candidate.point)
            candidate.point_valid=candidate.point_valid && std::isfinite(lane) && std::abs(lane)<1000000;
        if (!std::isfinite(candidate.speed) ||
            candidate.speed<0 || candidate.speed>300) continue;
        grounded=grounded || candidate.normal[1]/std::sqrt(norm)>.5f;
        if (!best || candidate.speed>best->speed) best=candidate;
    }
    if (best) {
        best->grounded=grounded;
        // The point is shared and either branch can overwrite it. With two
        // directions its association is unknown; retain only linear evidence.
        best->point_valid=best->point_valid && directions==1;
    }
    return best;
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
