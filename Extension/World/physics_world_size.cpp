#include "physics_world_size.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/physics_world_size.h"
#include "Engine/Core/Log/logging.h"

#include <Windows.h>
#include "Engine/Core/Hooks/hooks.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>

namespace dingosdk {
namespace {
namespace world_size = addr::physics_world_size;
using WorldCreate = void* (*)(void*, void*);
std::atomic<WorldCreate> original_create{};
std::uintptr_t installed_base{};
std::mutex install_mutex;

// Every pool a static triangle-mesh body is carved from. Contacts, joints,
// queries and culling tables track dynamic interactions and stay native.
constexpr std::size_t scaled_fields[]{
    world_size::geometry_count,
    world_size::owner_count,
    world_size::body_count,
    world_size::hull_and_mesh_count,
    world_size::shape_count,
    world_size::aggregate_shape_count,
    world_size::fixed_shape_count,
    world_size::fixed_partition_count,
    world_size::persistent_spatial_index_count,
};

bool read_memory(const void* source, void* destination, std::size_t size) noexcept {
    SIZE_T count{};
    return ReadProcessMemory(GetCurrentProcess(), source, destination, size, &count) && count == size;
}

bool validate_contract(std::uintptr_t base) noexcept {
    if (!base || base > std::numeric_limits<std::uintptr_t>::max() - supported_build::game_image_size)
        return false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!read_memory(reinterpret_cast<const void*>(base), &dos, sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
        dos.e_lfanew > 0x1000 ||
        !read_memory(reinterpret_cast<const void*>(base + dos.e_lfanew), &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage != supported_build::game_image_size) return false;
    for (const auto& contract : world_size::contracts) {
        std::array<std::uint8_t, world_size::max_contract_size> actual{};
        if (!read_memory(reinterpret_cast<const void*>(base + contract.rva), actual.data(), contract.bytes.size()) ||
            std::memcmp(actual.data(), contract.bytes.data(), contract.bytes.size()) != 0) return false;
    }
    return true;
}

std::uint32_t field(const std::uint8_t* size, std::size_t offset) noexcept {
    std::uint32_t value{};
    std::memcpy(&value, size + offset, sizeof(value));
    return value;
}

void* create_world(void* world, void* descriptor) {
    const auto incoming_error = GetLastError();
    const auto original = original_create.load(std::memory_order_acquire);
    alignas(16) std::array<std::uint8_t, world_size::size_bytes> size{};
    // The descriptor is the caller's stack local, rebuilt for every creation, so
    // raising it in place affects only this world. Absolute targets keep it idempotent.
    if (descriptor && read_memory(descriptor, size.data(), size.size())) {
        const auto native_size = size;
        const auto bodies = field(size.data(), world_size::body_count);
        if (bodies > 0 && bodies < physics_world_body_target) {
            const auto scale = (physics_world_body_target + bodies - 1) / bodies;
            for (const auto offset : scaled_fields) {
                const auto native = field(size.data(), offset);
                auto raised = offset == world_size::body_count
                    ? physics_world_body_target
                    : static_cast<std::uint32_t>(std::min<std::uint64_t>(
                          std::uint64_t{native} * scale, physics_world_pool_ceiling));
                raised = std::max(raised, native);
                std::memcpy(size.data() + offset, &raised, sizeof(raised));
            }
            std::memcpy(descriptor, size.data(), size.size());
            const auto pair = [&](std::size_t offset) {
                return std::format("{} -> {}", field(native_size.data(), offset), field(size.data(), offset));
            };
            logging::log(logging::Level::info, logging::Channel::world,
                "Physics world pools raised: bodies {}, shapes {}, fixed shapes {}, owners {}, "
                "geometries {}, hull/mesh {}, partitions {}, spatial indices {}",
                pair(world_size::body_count), pair(world_size::shape_count),
                pair(world_size::fixed_shape_count), pair(world_size::owner_count),
                pair(world_size::geometry_count), pair(world_size::hull_and_mesh_count),
                pair(world_size::fixed_partition_count), pair(world_size::persistent_spatial_index_count));
        }
    }
    SetLastError(incoming_error);
    return original(world, descriptor);
}
} // namespace

bool start_physics_world_size(std::uintptr_t base, std::string& error) {
    std::lock_guard lock(install_mutex);
    error.clear();
    if (installed_base) {
        if (installed_base == base) return true;
        error = "Physics world size support is already installed on another image";
        return false;
    }
    if (!validate_contract(base)) {
        error = "Physics world creation contract does not match the supported game";
        return false;
    }
    auto* target = reinterpret_cast<void*>(base + world_size::create_rva);
    WorldCreate original{};
    const auto created = hook_prepare(target, reinterpret_cast<void*>(&create_world),
        reinterpret_cast<void**>(&original));
    if (created != HookOk) {
        error = "Cannot create physics world hook (Detours hook service status " + std::to_string(created) + ")";
        return false;
    }
    if (!original) {
        hook_remove(target);
        error = "Physics world trampoline is unavailable";
        return false;
    }
    original_create.store(original, std::memory_order_release);
    const auto enabled = hook_enable(target);
    if (enabled != HookOk) {
        hook_remove(target);
        original_create.store(nullptr, std::memory_order_release);
        error = "Cannot enable physics world hook (Detours hook service status " + std::to_string(enabled) + ")";
        return false;
    }
    installed_base = base;
    return true;
}
} // namespace dingosdk
