#include "overlay_internal.h"
#include "Extension/Slam/slam_runtime.h"
#include "slam_mesh_renderer.h"

namespace dingosdk::overlay::detail {
void draw_slam() {
    if (!slam::hud_visible()) return;
    const auto value = slam::snapshot();
    if (!value.visible) return;
    const auto& result = value.result;
    ImGui::SetNextWindowPos(ImVec2(24, 110), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(.8f);
    ImGui::Begin("Slam Challenge HUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted("SLAM CHALLENGE");
    ImGui::Text("%s  |  %llu points", slam::phase_name(result.phase), static_cast<unsigned long long>(result.points));
    ImGui::Text("%u impacts  |  %u fractured regions", result.impacts, result.fractures);
    ImGui::Text("Fall %.1f m  |  Air %.1f s  |  Slide %.1f m", result.fall_m, result.airtime_s, result.slide_m);
    ImGui::TextUnformatted(result.detail.c_str());
    const auto render_status=slam_mesh_renderer_status();
    if (!value.mesh) ImGui::TextUnformatted(value.mesh_status.c_str());
    else if (render_status.starts_with("3D X-ray unavailable")) ImGui::TextUnformatted(render_status.data(),render_status.data()+render_status.size());
    if (value.first_person) ImGui::TextUnformatted("X-ray hidden in first person.");
    else if (!value.visuals.normal_play && !result.cancelled && result.phase==slam::Phase::results && !value.xray_context_valid)
        ImGui::TextUnformatted("Start a new attempt to show the X-ray on this skater.");
    if (result.phase == slam::Phase::results) {
        ImGui::Separator();
        ImGui::Text("Impacts: %llu  |  Bonuses: %llu", static_cast<unsigned long long>(result.impact_points),
            static_cast<unsigned long long>(result.bonus_points));
        for (std::size_t i = 0; i < slam::region_count; ++i) {
            if (result.injuries[i].severity <= 0) continue;
            ImGui::Text("%s: %s", slam::region_name(static_cast<slam::Region>(i)), result.injuries[i].fractured ? "fractured" : "bruised");
        }
        ImGui::TextUnformatted("Skater > Slam to retry or dismiss.");
    }
    ImGui::End();
}
}
