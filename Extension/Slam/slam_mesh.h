#pragma once
#include "slam_pose.h"
#include "Engine/Resource/ebx_document.h"
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace dingosdk::slam {
inline constexpr std::size_t render_bone_count = 386;
struct MeshVertex {
    Vec3 position{}, normal{};
    std::array<std::uint16_t,8> bones{};
    std::array<std::uint8_t,8> weights{};
    std::uint32_t region{};
    std::uint32_t part{};
};
static_assert(sizeof(MeshVertex) == 56);
struct SkeletonRig {
    std::array<int,render_bone_count> parents{};
    std::array<PoseMatrix,render_bone_count> inverse_bind{};
    std::array<Region,render_bone_count> regions{};
    std::array<unsigned,render_bone_count> physics_parts{};
};
struct SkeletonMesh {
    SkeletonRig rig;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    // Only skinning bones and their ancestors are needed. Board and unused
    // animation markers may have no initialized pose while walking.
    std::array<bool,render_bone_count> required{};
};
struct MeshGeometry {
    frostbite::Guid chunk;
    std::uint32_t vertex_bytes{}, index_bytes{};
    std::size_t lod{}, sections{};
    unsigned section_count{};
};
struct RenderCamera {
    PoseMatrix world{};
    float vertical_fov{};
};
// Supported native CurrentRenderView input (320 bytes), not the CPU camera
// entity. Fourth matrix lanes contain metadata and are cleaned for drawing.
bool decode_render_camera(std::span<const std::byte> input, RenderCamera& output) noexcept;
struct MeshPose {
    std::array<PoseMatrix,render_bone_count> skin;
    std::uint64_t at_ms{};
    std::uint64_t sequence{};
    bool render_export{};
    bool native_draw{};
    bool presentation_read{};
    // Immutable pair captured while the main raster view is submitted.
    // Never refresh only its camera or only its bones at Present.
    std::optional<RenderCamera> render_camera;
    std::uint64_t camera_at_ms{};
};
PoseMatrix compose_matrices(const PoseMatrix& local, const PoseMatrix& parent) noexcept;
SkeletonRig read_skeleton_rig(const frostbite::ebx::Document& document);
MeshGeometry read_mesh_geometry(std::span<const std::byte> resource);
SkeletonMesh read_skinned_mesh(std::span<const std::byte> resource, std::span<const std::byte> geometry,
    SkeletonRig rig);
SkeletonMesh load_dembones_mesh(const std::filesystem::path& game_root);
// All-or-nothing: failed or partially updated native poses cannot publish a
// mesh assembled from unrelated frames.
bool skin_render_pose(const SkeletonMesh& mesh, std::span<const std::array<float,12>> local,
    MeshPose& output) noexcept;
// Composed 4x4 skin matrices already include inverse bind. Apply only the
// placement required by their source; native fourth lanes carry SIMD metadata.
bool place_render_skin(const SkeletonMesh& mesh, std::span<const PoseMatrix> skin,
    const PoseMatrix& placement, MeshPose& output) noexcept;
// Final native draw packets use transposed 3x4 skinning matrices. Their
// placement must follow the packet's world/relative and extracted-root flags.
bool place_packed_render_skin(const SkeletonMesh& mesh, std::span<const std::array<float,12>> skin,
    const PoseMatrix& placement, MeshPose& output) noexcept;
// Correct delayed horizontal trajectory translation to the owned actor.
// Preserve animation height: the collider origin has separate vertical steps.
// Reject teleports/mismatches, including excessive vertical separation.
bool rebase_render_root(const MeshPose& source, const Vec3& root, MeshPose& output) noexcept;
}
