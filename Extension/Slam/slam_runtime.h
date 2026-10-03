#pragma once
#include "slam_model.h"
#include "slam_mesh.h"
#include "slam_visuals.h"
#include "slam_progression.h"
#include "slam_controls.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace dingosdk::slam {
enum class Action { start, stop, dismiss, bail };
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
    Config selected_config;
    BailControls bail_controls;
    bool bail_controls_ready{},bail_available{},bail_controller_conflict{},bail_key_conflict{};
    std::string bail_status,bail_controls_save_status;
    std::uint64_t manual_bails_queued{},manual_bails_selected{},manual_bails_published{};
    PersonalBest selected_best, result_best;
    std::string challenge_save_status, best_save_status;
    bool challenge_options_ready{}, result_best_ready{}, new_best{};
    VisualEvents visual_events;
    VisualEvents normal_xray_events;
    std::string visual_save_status;
    std::string impact_audio_status;
    std::string slow_motion_status;
    bool slow_motion_active{};
    bool impact_camera_available{};
    float slow_motion_factor = 1;
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
// Applies to the next attempt. The running challenge retains its own rules.
bool set_challenge_config(const Config& config) noexcept;
bool set_bail_controls(const BailControls& controls) noexcept;
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
// Restore temporary game speed before native loading tears down the scene.
void before_level_transition(unsigned next) noexcept;
void observe_selection(std::uintptr_t selector, std::uint32_t next) noexcept;
void observe_manual_bail(std::uintptr_t selector) noexcept;
void observe_skeleton(std::uintptr_t rig, float seconds, bool wipeout) noexcept;
// Shared native render publication hook: final animation pose for visuals,
// independently of whether the off-board physics skeleton updated this frame.
void observe_render(std::uintptr_t animation_interface, std::uintptr_t render_data,std::uintptr_t caller) noexcept;
void observe_animation(std::uintptr_t component) noexcept;
}
