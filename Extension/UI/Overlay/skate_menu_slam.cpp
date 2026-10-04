#include "skate_menu_internal.h"
#include "Extension/Slam/slam_runtime.h"
#include <algorithm>
#include <cmath>

namespace dingosdk::overlay::menu {
namespace {
bool running(slam::Phase phase) {
    return phase==slam::Phase::attempt || phase==slam::Phase::bailed || phase==slam::Phase::settled;
}
bool integer_target(slam::ChallengeKind kind) {
    return kind==slam::ChallengeKind::score || kind==slam::ChallengeKind::chain || kind==slam::ChallengeKind::fractures;
}
void progress(const slam::Result& result) {
    if (result.config.kind==slam::ChallengeKind::free_play) return;
    const bool whole=integer_target(result.config.kind);
    ImGui::Text(whole ? "%.0f / %.0f %s" : "%.1f / %.1f %s",result.target_progress,result.config.target,
        slam::challenge_unit(result.config.kind));
    ImGui::ProgressBar(std::clamp(result.target_progress/result.config.target,0.f,1.f),ImVec2(-FLT_MIN,0),
        result.target_met ? "Target reached" : "Target progress");
}
bool rule(SkateMenu& menu,const char* label,float& value,float low,float high,const char* format="%.0f") {
    field(menu,label);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed=ImGui::DragFloat("##rule",&value,high<=5 ? .01f : 1.f,low,high,format,ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopID();
    return changed;
}
void best(const slam::PersonalBest& record,slam::ChallengeKind kind) {
    if (!record.attempts) { note("No completed attempts for this map, target and rule set yet."); return; }
    ImGui::Text("Best score: %llu points",static_cast<unsigned long long>(record.score));
    if (kind!=slam::ChallengeKind::free_play)
        ImGui::Text(integer_target(kind) ? "Best progress: %.0f %s" : "Best progress: %.1f %s",
            record.progress,slam::challenge_unit(kind));
    ImGui::Text("%u completed  |  %u targets reached",record.attempts,record.successes);
}
void breakdown(const slam::Result& result) {
    if (!ImGui::BeginTable("score-breakdown",2,ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("Source",ImGuiTableColumnFlags_WidthStretch,2);
    ImGui::TableSetupColumn("Points",ImGuiTableColumnFlags_WidthStretch,1);
    ImGui::TableHeadersRow();
    const auto row=[](const char* label,std::uint64_t points) {
        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
        ImGui::TableNextColumn(); ImGui::Text("%llu",static_cast<unsigned long long>(points));
    };
    row("Body impacts",result.impact_points);
    row("Impact chains",result.chain_points);
    row("Fractures",result.fracture_points);
    row("Fall distance",result.fall_points);
    row("Airtime",result.airtime_points);
    row("Sliding",result.slide_points);
    row("Total",result.points);
    ImGui::EndTable();
}
}
void slam_challenge_cards(SkateMenu& menu,const slam::Snapshot& value) {
    const auto& result=value.result;
    const bool active=running(result.phase);
    begin_card(menu,"slam-challenge","CHOOSE A CHALLENGE","Local Slam");
    auto config=value.selected_config;
    bool changed=false;
    ImGui::BeginDisabled(!value.challenge_options_ready);
    field(menu,"Challenge"); ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##slam-kind",slam::challenge_name(config.kind))) {
        for (unsigned i=0;i<static_cast<unsigned>(slam::ChallengeKind::count);++i) {
            const auto kind=static_cast<slam::ChallengeKind>(i);
            const bool selected=kind==config.kind;
            if (ImGui::Selectable(slam::challenge_name(kind),selected) && !selected) {
                config.kind=kind; config.target=slam::default_target(kind); changed=true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (config.kind!=slam::ChallengeKind::free_play) {
        field(menu,"Target",slam::challenge_unit(config.kind)); ImGui::SetNextItemWidth(-FLT_MIN);
        if (integer_target(config.kind)) {
            int target=static_cast<int>(config.target);
            const int high=config.kind==slam::ChallengeKind::fractures ? static_cast<int>(slam::region_count) : 1000000;
            if (ImGui::DragInt("##slam-target",&target,1,1,high,"%d",ImGuiSliderFlags_AlwaysClamp)) {
                config.target=static_cast<float>(target); changed=true;
            }
        } else changed|=ImGui::DragFloat("##slam-target",&config.target,.1f,1.f,1000000.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
        note(slam::challenge_unit(config.kind));
    } else note("No target. Build the highest score from a single fall.");
    if (ImGui::TreeNode("Scoring rules")) {
        auto& rules=config.scoring;
        changed|=rule(menu,"Impact points per severity",rules.impact_rate,0,10000);
        changed|=rule(menu,"Bonus per fractured region",rules.fracture_bonus,0,10000);
        changed|=rule(menu,"Fall points per metre",rules.fall_rate,0,10000);
        changed|=rule(menu,"Air points per second",rules.airtime_rate,0,10000);
        changed|=rule(menu,"Slide points per metre",rules.slide_rate,0,10000);
        float percent=rules.chain_step*100;
        if (rule(menu,"Chain bonus per extra hit",percent,0,100,"%.0f%%")) { rules.chain_step=percent/100; changed=true; }
        changed|=rule(menu,"Chain window",rules.chain_window_s,.25f,5,"%.2f seconds");
        changed|=rule(menu,"Bruise threshold",rules.bruise_threshold,0,std::min(rules.head_fracture,rules.limb_fracture));
        changed|=rule(menu,"Head fracture threshold",rules.head_fracture,std::max(10.f,rules.bruise_threshold),10000);
        changed|=rule(menu,"Other fracture threshold",rules.limb_fracture,std::max(10.f,rules.bruise_threshold),10000);
        note("Severity measures the squared speed lost into a confirmed contact. Harder hits score more. "
             "Chains add a bonus to each hit while the window lasts, capped at +300%. "
             "Each of the six body regions awards its fracture bonus once.");
        if (ImGui::Button("Reset scoring rules",ImVec2(-FLT_MIN,0))) { rules={}; changed=true; }
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
    if (changed && !slam::set_challenge_config(config)) feedback(menu,"Challenge settings could not be applied.");
    if (active) note("These settings apply to the next attempt. The current attempt keeps its original target and rules.");
    if (!value.challenge_save_status.empty()) note(value.challenge_save_status.c_str());
    if (!changed && value.challenge_options_ready && value.available) best(value.selected_best,config.kind);
    note("Personal bests are separate for each map, challenge, target and scoring rule set.");
    end_card();

    begin_card(menu,"slam-attempt","ATTEMPT");
    ImGui::Text("%s  |  %llu points",slam::phase_name(result.phase),static_cast<unsigned long long>(result.points));
    if (result.phase!=slam::Phase::ready) {
        note(slam::challenge_name(result.config.kind));
        if (!result.cancelled) progress(result);
    }
    note(result.detail.c_str());
    if (!value.available || value.bailed) warn(value.availability.c_str());
    if (!active && !value.retry_active && primary_button(menu,result.phase==slam::Phase::ready ? "Start attempt" : "Start new attempt here",
        value.available && !value.bailed && value.challenge_options_ready))
        (void)slam::request(slam::Action::start);
    if (value.saved_start && !active && !value.retry_active &&
        primary_button(menu,"Retry from saved start",value.retry_available)) (void)slam::request(slam::Action::retry);
    if (value.retry_active && ImGui::Button("Cancel retry",ImVec2(-FLT_MIN,0))) (void)slam::request(slam::Action::stop);
    if (!value.retry_status.empty()) note(value.retry_status.c_str());
    if (value.saved_start) note("Retry uses the original attempt's location, orientation, challenge and rules. Start new attempt here uses your selected settings.");
    ImGui::BeginDisabled(!value.bail_available);
    if (ImGui::Button("Bail now",ImVec2(-FLT_MIN,0))) (void)slam::request(slam::Action::bail);
    ImGui::EndDisabled();
    note(value.bail_status.c_str());
    if (active && ImGui::Button("Cancel attempt",ImVec2(-FLT_MIN,0))) (void)slam::request(slam::Action::stop);
    if (value.visible && ImGui::Button(active ? "Exit Slam" : "Dismiss results",ImVec2(-FLT_MIN,0)))
        (void)slam::request(slam::Action::dismiss);
    note("Start, close the menu and take a fall. Let the skater settle or recover to finish. Canceling discards the score.");
    note(value.mesh_status.c_str());
    end_card();

    if (result.phase!=slam::Phase::results) return;
    begin_card(menu,"slam-results","RESULTS",slam::challenge_name(result.config.kind));
    if (result.cancelled) {
        warn("Attempt canceled. No score or personal best was recorded.");
        note(result.detail.c_str());
    } else {
        if (result.config.kind!=slam::ChallengeKind::free_play) {
            if (result.target_met) tag(menu,"TARGET REACHED",IM_COL32(82,203,128,255));
            else note("Target missed. Your score still counts toward this rule set's personal best.");
            progress(result);
        }
        if (value.result_best_ready && value.new_best) tag(menu,"NEW PERSONAL BEST",IM_COL32(100,183,255,255));
        breakdown(result);
        ImGui::Text("%u impacts  |  Best chain: %u  |  %.1f seconds",result.impacts,result.best_chain,result.elapsed_s);
        ImGui::Text("Fall: %.1f m  |  Air: %.1f s  |  Slide: %.1f m",result.fall_m,result.airtime_s,result.slide_m);
        ImGui::Separator();
        for (std::size_t i=0;i<slam::region_count;++i) {
            const auto& injury=result.injuries[i];
            ImGui::Text("%s: %s",slam::region_name(static_cast<slam::Region>(i)),
                slam::injury_state_name(injury,result.config.scoring));
        }
        if (value.result_best_ready) best(value.result_best,result.config.kind);
        else note("Recording this result...");
        if (!value.best_save_status.empty()) note(value.best_save_status.c_str());
    }
    end_card();
}
}
