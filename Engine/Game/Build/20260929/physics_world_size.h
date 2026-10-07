// Reviewed native contract for supported_build::game_sha256 (September 29 2026).
// fb::FBPhysicsWorld creation copies the 0xa8-byte FBPhysicsWorldSize at the
// front of its descriptor into the job arguments (+0x2428) before any pool,
// free list or per-body array is sized from it. Both native callers build that
// descriptor on their own stack immediately before the call.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dingosdk::game::build::v20260929::physics_world_size {
template<std::size_t N> consteval auto hex_bytes(const char (&hex)[N]) {
    static_assert(N % 2 == 1);
    std::array<std::uint8_t, (N - 1) / 2> result{};
    const auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        throw "Invalid physics world size contract hex";
    };
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
    return result;
}

// World creation entry, through the stack cookie.
inline constexpr auto create_entry = hex_bytes(
    "48895c241848897424205741544155415641574881eca0030000c5f829b42490030000488b05a6d3bb044833c4"
    "4889842480030000");
// rbx = this->m_jobArguments; copy descriptor[0x00..0xa8) to rbx+0x2428.
inline constexpr auto size_copy = hex_bytes(
    "488b5e28c4c17c1006c5fc118328240000c4c17c104e20c5fc118b48240000c4c17c104640c5fc118368240000"
    "c4c17c104e60c5fc118b88240000c4c17c108680000000c5fc1183a8240000c4c17b108ea0000000c5fb118bc8"
    "240000");
// The world reserves one internal body on top of BodyCount (data 8200 -> 8201 slots).
inline constexpr auto internal_body = hex_bytes("ff833024000083832824000003");
// Both callers: lea rdx,[stack descriptor]; mov rcx,rax; call create.
inline constexpr auto caller_a_call = hex_bytes("e851195200");
inline constexpr auto caller_b_call = hex_bytes("e8306f8dfe");

struct Contract {
    std::uintptr_t rva;
    std::span<const std::uint8_t> bytes;
};
inline constexpr Contract contracts[]{
    {0x02606ff0, create_entry},
    {0x02607fec, size_copy},
    {0x026080b0, internal_body},
    {0x020e569a, caller_a_call},
    {0x03d300bb, caller_b_call},
};
inline constexpr std::size_t max_contract_size = [] {
    std::size_t size = 0;
    for (const auto& contract : contracts) size = contract.bytes.size() > size ? contract.bytes.size() : size;
    return size;
}();

// unsigned-int fields of FBPhysicsWorldSize in reflected field order (index * 4).
inline constexpr std::size_t size_bytes = 0xa8;
inline constexpr std::size_t geometry_count = 0x00;
inline constexpr std::size_t owner_count = 0x04;
inline constexpr std::size_t body_count = 0x08;
inline constexpr std::size_t hull_and_mesh_count = 0x14;
inline constexpr std::size_t shape_count = 0x18;
inline constexpr std::size_t aggregate_shape_count = 0x1c;
inline constexpr std::size_t fixed_shape_count = 0x20;
inline constexpr std::size_t fixed_partition_count = 0x3c;
inline constexpr std::size_t persistent_spatial_index_count = 0xa0;

// fb::FBPhysicsWorld creation: (void* world, Descriptor* descriptor).
inline constexpr std::uintptr_t create_rva = 0x02606ff0;
} // namespace dingosdk::game::build::v20260929::physics_world_size
