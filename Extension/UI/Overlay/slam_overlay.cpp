#include "overlay_internal.h"
#include "Extension/Slam/slam_runtime.h"
#include "slam_mesh_renderer.h"
#include "Engine/Core/Platform/launcher_support.h"
#include <algorithm>

namespace dingosdk::overlay::detail {
void draw_slam() {
    if (!slam::hud_visible()) return;
    const auto value = slam::snapshot();
    if (!value.visible) return;
    const auto& result = value.result;
    ImGui::SetNextWindowPos(ImVec2(24, 110), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(360,0),ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(.8f);
    ImGui::Begin("Slam Challenge HUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(slam::challenge_name(result.phase==slam::Phase::ready ? value.selected_config.kind : result.config.kind));
    ImGui::Text("%s  |  %llu points", slam::phase_name(result.phase), static_cast<unsigned long long>(result.points));
    if (!result.cancelled && result.phase!=slam::Phase::ready) {
        if (result.config.kind!=slam::ChallengeKind::free_play) {
            const bool integer=result.config.kind==slam::ChallengeKind::score || result.config.kind==slam::ChallengeKind::chain ||
                result.config.kind==slam::ChallengeKind::fractures;
            ImGui::Text(integer ? "%.0f / %.0f %s" : "%.1f / %.1f %s",result.target_progress,result.config.target,
                slam::challenge_unit(result.config.kind));
            ImGui::ProgressBar(std::clamp(result.target_progress/result.config.target,0.f,1.f),ImVec2(-FLT_MIN,0),
                result.target_met ? "Target reached" : "Target progress");
        }
        ImGui::Text("%u impacts  |  %u fractured regions", result.impacts, result.fractures);
        ImGui::Text("Fall %.1f m  |  Air %.1f s  |  Slide %.1f m", result.fall_m, result.airtime_s, result.slide_m);
        if (result.phase!=slam::Phase::results)
            ImGui::Text("Chain: %u  |  Best: %u  |  Window: %.1f s",result.current_chain,result.best_chain,result.chain_remaining_s);
    }
    ImGui::TextUnformatted(result.detail.c_str());
    if (value.retry_active) ImGui::TextUnformatted(value.retry_status.c_str());
    if (result.phase==slam::Phase::attempt && value.bail_available) {
        if (value.bail_controls.key && !value.bail_key_conflict)
            ImGui::Text("Bail: %s",launcher::key_name(value.bail_controls.key).c_str());
        if (value.bail_controls.controller_combo && !value.bail_controller_conflict)
            ImGui::Text("Bail: %s",controller_combo_label(value.bail_controls.controller_combo).c_str());
    }
    const auto render_status=slam_mesh_renderer_status();
    if (!value.mesh) ImGui::TextUnformatted(value.mesh_status.c_str());
    else if (render_status.starts_with("3D X-ray unavailable")) ImGui::TextUnformatted(render_status.data(),render_status.data()+render_status.size());
    if (value.first_person) ImGui::TextUnformatted("X-ray hidden in first person.");
    else if (!value.visuals.normal_play && !result.cancelled && result.phase==slam::Phase::results && !value.xray_context_valid)
        ImGui::TextUnformatted("Start a new attempt to show the X-ray on this skater.");
    if (result.phase == slam::Phase::results && !result.cancelled) {
        ImGui::Separator();
        if (result.config.kind!=slam::ChallengeKind::free_play && !result.target_met) ImGui::TextUnformatted("Target missed");
        if (value.result_best_ready) {
            if (value.new_best) ImGui::TextUnformatted("NEW PERSONAL BEST");
            ImGui::Text("Best score: %llu",static_cast<unsigned long long>(value.result_best.score));
        }
        ImGui::Text("Impacts %llu  |  Chains %llu  |  Fractures %llu",static_cast<unsigned long long>(result.impact_points),
            static_cast<unsigned long long>(result.chain_points),static_cast<unsigned long long>(result.fracture_points));
        ImGui::Text("Fall %llu  |  Air %llu  |  Slide %llu",static_cast<unsigned long long>(result.fall_points),
            static_cast<unsigned long long>(result.airtime_points),static_cast<unsigned long long>(result.slide_points));
        ImGui::Text("Best chain: %u impacts",result.best_chain);
        for (std::size_t i = 0; i < slam::region_count; ++i) {
            if (result.injuries[i].severity <= 0) continue;
            ImGui::Text("%s: %s", slam::region_name(static_cast<slam::Region>(i)), result.injuries[i].fractured ? "fractured" : "bruised");
        }
        if (!value.best_save_status.empty()) ImGui::TextUnformatted(value.best_save_status.c_str());
    }
    if (result.phase==slam::Phase::results) {
        if (result.cancelled) ImGui::TextUnformatted("Canceled: no score or personal best recorded.");
        ImGui::TextUnformatted("Skater > Slam for results and the next attempt.");
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
}
}
