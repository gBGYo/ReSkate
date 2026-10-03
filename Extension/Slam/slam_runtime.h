#pragma once
#include "slam_model.h"
#include "slam_mesh.h"
#include "slam_visuals.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace dingosdk::slam {
enum class Action { start, stop, dismiss };
struct Joint { Vec3 position{}; Region region = Region::torso; int parent = -1; };
struct Snapshot {
    Result result;
    Result normal_xray_result;
    std::array<Joint, 24> joints{};
    std::string availability = "Waiting for an offline local skater.";
    std::string mesh_status = "Dem Bones mesh has not been loaded.";
    std::shared_ptr<const SkeletonMesh> mesh;
    std::shared_ptr<const MeshPose> mesh_pose;
    VisualOptions visuals;
    VisualEvents visual_events;
    VisualEvents normal_xray_events;
    std::string visual_save_status;
    std::uint64_t samples{}, dropped{}, pose_at_ms{};
    std::uint64_t rendered_poses{}, rejected_poses{};
    std::uint64_t animation_poses{}, export_poses{}, superseded_poses{};
    std::uintptr_t image_base{};
    unsigned contacts{};
    std::uint32_t physics_state{};
    bool available{}, visible{}, pose_valid{}, bailed{}, first_person{}, visual_options_ready{}, xray_context_valid{}, normal_xray_available{};
};
// Presentation/console threads only queue controls and copy immutable snapshots.
bool request(Action action) noexcept;
bool set_visual_options(const VisualOptions& options) noexcept;
Snapshot snapshot();
// Consumes the pending render-export pose for this scene presentation.
Snapshot presentation_snapshot();
bool hud_visible() noexcept;
bool visuals_visible() noexcept;
// Game thread publishes a short-lived, verified local-owner watch. Shared bail
// hooks observe without changing the native selection, contacts or ragdoll.
void tick(std::uintptr_t base, std::uintptr_t client, std::uintptr_t entity,
    bool ready, bool offline, bool no_bail, bool noclip, bool editor, bool first_person,
    std::string_view map) noexcept;
void observe_selection(std::uintptr_t selector, std::uint32_t next) noexcept;
void observe_skeleton(std::uintptr_t rig, float seconds, bool wipeout) noexcept;
// Shared native render publication hook: final animation pose for visuals,
// independently of whether the off-board physics skeleton updated this frame.
void observe_render(std::uintptr_t animation_interface, std::uintptr_t render_data,std::uintptr_t caller) noexcept;
void observe_animation(std::uintptr_t component) noexcept;
}
