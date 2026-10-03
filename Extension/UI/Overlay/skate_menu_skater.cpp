#include "skate_menu_internal.h"
#include "Extension/Slam/slam_runtime.h"
#include "Extension/UI/Overlay/input_capture.h"
#include "Engine/Core/Platform/launcher_support.h"

#include <array>
#include <cmath>

// The SKATER page.
namespace dingosdk::overlay::menu {
namespace {
// A slider row with a trailing button (e.g. "Default") after a field() label.
float trailing_width(const char* label) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
}
}
void camera_controls(SkateMenu& menu, const Model& model, const CallbacksV3& callbacks) {
    const auto& debug = model.debug;
    begin_card(menu,"normal-xray","X-RAY");
    const auto xray=dingosdk::slam::snapshot();
    auto visuals=xray.visuals;
    if (toggle_row(menu,"X-ray in normal play","Show the skeleton without starting a Slam attempt.",visuals.normal_play,xray.visual_options_ready))
        (void)dingosdk::slam::set_visual_options(visuals);
    note("Opacity, visibility and impacted bones: Skater > Slam > X-ray. Falls reset automatically after recovery.");
    if (visuals.normal_play && !xray.normal_xray_available) note(xray.availability.c_str());
    if (xray.first_person && visuals.normal_play) note("The skeleton is hidden while First person is enabled.");
    end_card();
    begin_card(menu, "freecam", "FREECAM");
    bool flight = debug.free_camera;
    if (toggle_row(menu, "Freecam", "Detach the camera and explore.", flight,
            debug.available && debug.camera_available && callbacks.queue_debug))
        debug_request(menu, callbacks, {DebugAction::set_free_camera, flight});
    {
        field(menu, "Field of view");
        const bool custom = debug.free_camera_fov > 0;
        int free_fov = static_cast<int>(std::lround(custom ? debug.free_camera_fov : debug.camera_fov));
        const float reset = trailing_width("Default");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - reset - ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginDisabled(!callbacks.queue_debug);
        if (ImGui::SliderInt("##free-camera-fov", &free_fov, 40, 120, custom ? "%d degrees" : "%d degrees (game)",
                ImGuiSliderFlags_AlwaysClamp))
            debug_request(menu, callbacks, {DebugAction::set_free_camera_fov, false, static_cast<float>(free_fov)});
        ImGui::SameLine();
        ImGui::BeginDisabled(!custom);
        if (ImGui::Button("Default", ImVec2(reset, 0)))
            debug_request(menu, callbacks, {DebugAction::set_free_camera_fov, false, 0.0f});
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    field(menu, "Flight speed", "Also used by Noclip.");
    ImGui::BeginDisabled((!debug.free_camera && !debug.noclip) || !debug.camera_available || !callbacks.queue_debug);
    constexpr std::array<float, 7> speeds{0.6f, 3, 5, 15, 60, 300, 1500};
    constexpr std::array<const char*, 7> labels{"Precise", "Slow", "Cruise", "Default", "Fast", "Travel", "Maximum"};
    int selected = 3;
    for (int i = 0; i < static_cast<int>(speeds.size()); ++i)
        if (std::abs(debug.camera_speed - speeds[i]) < .01f) selected = i;
    if (ImGui::SliderInt("##flight-speed", &selected, 0, 6, labels[selected], ImGuiSliderFlags_NoInput))
        debug_request(menu, callbacks, {DebugAction::set_camera_speed, false, speeds[selected]});
    ImGui::EndDisabled();
    if (debug.free_camera) note("Close the menu to fly. WASD / Q E move; hold the right mouse button to look.");
    if (!debug.camera_available) warn(debug.camera_unavailable.c_str());
    end_card();

    begin_card(menu, "first-person", "FIRST PERSON");
    bool first_person = debug.first_person;
    if (toggle_row(menu, "First person", "Attach the camera to the skater's head.", first_person,
            debug.available && debug.camera_available && callbacks.queue_debug))
        debug_request(menu, callbacks, {DebugAction::set_first_person, first_person});
    field(menu, "Field of view");
    int fov = static_cast<int>(std::lround(debug.first_person_fov > 0 ? debug.first_person_fov : debug.camera_fov));
    ImGui::BeginDisabled(!debug.first_person || !callbacks.queue_debug);
    if (ImGui::SliderInt("##first-person-fov", &fov, 40, 120, "%d degrees", ImGuiSliderFlags_AlwaysClamp))
        debug_request(menu, callbacks, {DebugAction::set_first_person_fov, false, static_cast<float>(fov)});
    ImGui::EndDisabled();
    if (ImGui::TreeNode("Spring arm")) {
        namespace fp = dingosdk::first_person;
        auto settings = debug.first_person_arm;
        if (toggle_row(menu, "Spring", "Let the camera lag behind and soften animated head movement.", settings.enabled,
                debug.available && callbacks.queue_debug))
            debug_request(menu, callbacks, {DebugAction::set_first_person_spring, settings.enabled});
        ImGui::BeginDisabled(!debug.available || !callbacks.queue_debug);
        const auto slider = [&](const char* label, float value, float low, float high, const char* format, DebugAction action) {
            field(menu, label);
            ImGui::PushID(label);
            if (ImGui::SliderFloat("##arm-value", &value, low, high, format, ImGuiSliderFlags_AlwaysClamp))
                debug_request(menu, callbacks, {action, false, value});
            ImGui::PopID();
        };
        note("Position offset, metres from the head");
        slider("Right", settings.offset[0], -fp::offset_limit, fp::offset_limit, "%.3f", DebugAction::set_first_person_offset_x);
        slider("Up", settings.offset[1], -fp::offset_limit, fp::offset_limit, "%.3f", DebugAction::set_first_person_offset_y);
        slider("Forward", settings.offset[2], -fp::offset_limit, fp::offset_limit, "%.3f", DebugAction::set_first_person_offset_z);
        note("Rotation offset, degrees");
        slider("Pitch", settings.rotation[0], -fp::rotation_limit, fp::rotation_limit, "%.1f", DebugAction::set_first_person_pitch);
        slider("Yaw", settings.rotation[1], -fp::rotation_limit, fp::rotation_limit, "%.1f", DebugAction::set_first_person_yaw);
        slider("Roll", settings.rotation[2], -fp::rotation_limit, fp::rotation_limit, "%.1f", DebugAction::set_first_person_roll);
        note("Smoothing strength");
        slider("Moving up", settings.up, 0, fp::strength_limit, "%.0f%%", DebugAction::set_first_person_spring_up);
        slider("Moving down", settings.down, 0, fp::strength_limit, "%.0f%%", DebugAction::set_first_person_spring_down);
        slider("Moving left", settings.left, 0, fp::strength_limit, "%.0f%%", DebugAction::set_first_person_spring_left);
        slider("Moving right", settings.right, 0, fp::strength_limit, "%.0f%%", DebugAction::set_first_person_spring_right);
        if (ImGui::Button("Reset arm settings", ImVec2(-FLT_MIN, 0)))
            debug_request(menu, callbacks, {DebugAction::reset_first_person_arm});
        ImGui::EndDisabled();
        note("Higher smoothing follows more slowly; 0% follows directly. Offsets work with Spring off. "
             "Depth and roll use the average strength.");
        ImGui::TreePop();
    }
    end_card();
}

void movement_controls(SkateMenu& menu, const Model& model, const CallbacksV3& callbacks) {
    const auto& debug = model.debug;
    begin_card(menu, "movement", "MOVEMENT");
    bool noclip = debug.noclip;
    if (toggle_row(menu, "Noclip", "Fly with the normal player camera. Includes No Bail; uses the Freecam flight speed.", noclip,
            (debug.noclip_available || debug.noclip) && callbacks.queue_debug))
        debug_request(menu, callbacks, {DebugAction::set_noclip, noclip});
    if (!debug.noclip_available && !debug.noclip) note(debug.noclip_unavailable.c_str());
    bool no_bail = debug.no_bail;
    const char* bail_help = debug.noclip && debug.no_bail_active
        ? "Protection is automatic during noclip. Enable this to keep it when flight ends."
        : debug.no_bail && !debug.no_bail_active
        ? "Enabled; waiting for an active local skater."
        : "Prevent new wipeouts. Recover from any current bail before enabling.";
    if (toggle_row(menu, "No Bail", bail_help, no_bail,
            (debug.no_bail_available || debug.no_bail) && callbacks.queue_debug))
        debug_request(menu, callbacks, {DebugAction::set_no_bail, no_bail});
    end_card();

    begin_card(menu, "boosts", "BOOSTS", "Buttons are set in Settings > Controls");
    ImGui::BeginDisabled(!callbacks.queue_debug);
    field(menu, "Forward boost");
    float forward_velocity = debug.forward_velocity_speed;
    if (ImGui::SliderFloat("##forward-velocity", &forward_velocity, 1.0f, 300.0f, "+%.1f", ImGuiSliderFlags_AlwaysClamp))
        debug_request(menu, callbacks, {DebugAction::set_forward_velocity_speed, false, forward_velocity});
    field(menu, "Up boost");
    float up_velocity = debug.up_velocity_speed;
    if (ImGui::SliderFloat("##up-velocity", &up_velocity, 1.0f, 25.0f, "+%.1f", ImGuiSliderFlags_AlwaysClamp))
        debug_request(menu, callbacks, {DebugAction::set_up_velocity_speed, false, up_velocity});
    ImGui::EndDisabled();
    note("Controller: left stick moves, right stick looks, RT / LT rise and fall, click the left stick to boost.");
    note("Keyboard: WASD / Q E, Shift to boost. Close the menu to fly.");
    end_card();
}

void skater_page(SkateMenu& menu, const Model& model, const CallbacksV3& callbacks) {
    category_tabs(menu, menu.skater_tab, {"CAMERA", "MOVEMENT", "SLAM"}, "skater-tabs");
    ImGui::PushID(menu.skater_tab);
    ImGui::BeginChild("skater-tab", ImVec2(0, page_body_height(menu)));
    if (menu.skater_tab == 0) camera_controls(menu, model, callbacks);
    else if (menu.skater_tab == 1) movement_controls(menu, model, callbacks);
    else {
        const auto value = dingosdk::slam::snapshot();
        begin_card(menu, "slam-challenge", "SLAM CHALLENGE", "Offline prototype");
        note("Start an attempt, then take a fall. Impacts, fractures, fall height, airtime and sliding add to your score.");
        ImGui::Text("%s  |  %llu points", dingosdk::slam::phase_name(value.result.phase),
            static_cast<unsigned long long>(value.result.points));
        note(value.result.detail.c_str());
        if (!value.available || value.bailed) warn(value.availability.c_str());
        ImGui::BeginDisabled(!value.available || value.bailed);
        if (ImGui::Button(value.result.phase == dingosdk::slam::Phase::ready ? "Start attempt" : "Retry from here", ImVec2(-FLT_MIN, 0)))
            (void)dingosdk::slam::request(dingosdk::slam::Action::start);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!value.bail_available);
        if (ImGui::Button("Bail now",ImVec2(-FLT_MIN,0))) (void)dingosdk::slam::request(dingosdk::slam::Action::bail);
        ImGui::EndDisabled();
        note(value.bail_status.c_str());
        if (value.result.phase == dingosdk::slam::Phase::attempt || value.result.phase == dingosdk::slam::Phase::bailed ||
            value.result.phase == dingosdk::slam::Phase::settled) {
            if (ImGui::Button("Stop attempt", ImVec2(-FLT_MIN, 0))) (void)dingosdk::slam::request(dingosdk::slam::Action::stop);
        }
        if (value.visible && ImGui::Button("Dismiss HUD", ImVec2(-FLT_MIN, 0))) (void)dingosdk::slam::request(dingosdk::slam::Action::dismiss);
        note("Recover and get back on your board before retrying. Dem Bones supplies the 3D X-ray skeleton and injury highlights.");
        note(value.mesh_status.c_str());
        end_card();
        begin_card(menu,"slam-controls","BAIL CONTROLS");
        auto controls=value.bail_controls;
        bool controls_changed=false;
        ImGui::BeginDisabled(!value.bail_controls_ready);
        controls_changed|=toggle_row(menu,"Manual bail","Trigger a native wipeout during attempts or normal-play X-ray.",controls.enabled);
        ControllerInput controller;
        DingoSDKOverlayReadControllerInput(&controller,true);
        DWORD foreground_process{};
        GetWindowThreadProcessId(GetForegroundWindow(),&foreground_process);
        if (menu.slam_bind_capture && (ImGui::GetTime()>=menu.slam_capture_until ||
            foreground_process!=GetCurrentProcessId() || !value.bail_controls_ready)) menu.slam_bind_capture=0;
        field(menu,"Bail key");
        const auto key_label=(menu.slam_bind_capture==1 ? std::string("Press a key...") :
            controls.key ? launcher::key_name(controls.key) : std::string("Not bound"))+"###slam-key";
        if (ImGui::Button(key_label.c_str(),ImVec2(-FLT_MIN,0))) {
            menu.slam_bind_capture=1; menu.slam_capture_until=ImGui::GetTime()+30;
            detail::OverlayInputAccess access;
            for (int key=0;key<256;++key) menu.slam_keys_down[key]=(GetAsyncKeyState(key)&0x8000)!=0;
        }
        if (ImGui::SmallButton("Clear key")) {controls.key=0; controls_changed=true; menu.slam_bind_capture=0;}
        field(menu,"Bail controller combo");
        const auto pad_label=(menu.slam_bind_capture==2 ? std::string("Recording...") :
            controller_combo_label(controls.controller_combo,controller.style))+"###slam-controller";
        if (ImGui::Button(pad_label.c_str(),ImVec2(-FLT_MIN,0))) {
            menu.slam_bind_capture=2; menu.slam_controller_capture={}; menu.slam_capture_until=ImGui::GetTime()+30;
        }
        if (ImGui::SmallButton("Clear controller combo")) {controls.controller_combo=0; controls_changed=true; menu.slam_bind_capture=0;}
        if (menu.slam_bind_capture) {
            detail::OverlayInputAccess access;
            if (GetAsyncKeyState(VK_ESCAPE)&0x8000) menu.slam_bind_capture=0;
            if (menu.slam_bind_capture==1) {
                const auto keys=launcher::overlay_keys();
                for (int key=1;key<256;++key) {
                    const bool down=(GetAsyncKeyState(key)&0x8000)!=0;
                    const bool pressed=down && !menu.slam_keys_down[key];
                    menu.slam_keys_down[key]=down;
                    if (!pressed || !dingosdk::slam::valid_bail_key(static_cast<unsigned>(key)) ||
                        static_cast<unsigned>(key)==keys.menu || static_cast<unsigned>(key)==keys.console) continue;
                    controls.key=static_cast<unsigned>(key); controls_changed=true; menu.slam_bind_capture=0; break;
                }
            } else if (menu.slam_bind_capture==2) {
                if (const auto combo=menu.slam_controller_capture.update(controller)) {
                    controls.controller_combo=*combo; controls_changed=true; menu.slam_bind_capture=0;
                }
            }
            if (menu.slam_bind_capture) {
                note(menu.slam_bind_capture==1 ? "Press a key. Menu/console keys and modifiers are reserved. Escape cancels." :
                    !controller.available ? "Connect a controller. Escape cancels." :
                    !menu.slam_controller_capture.ready ? "Release all controller buttons first." :
                    "Hold the desired combo, then release it to save. Escape cancels.");
                if (ImGui::SmallButton("Cancel recording")) menu.slam_bind_capture=0;
            }
        }
        if (ImGui::Button("Reset bail controls",ImVec2(-FLT_MIN,0))) {controls={}; controls_changed=true; menu.slam_bind_capture=0;}
        ImGui::EndDisabled();
        if (controls_changed) (void)dingosdk::slam::set_bail_controls(controls);
        if (value.bail_key_conflict) warn("Bail key overlaps your menu or console key; choose another key.");
        if (value.bail_controller_conflict) warn("Bail combo overlaps Noclip or a velocity boost and is disabled; choose a different combo.");
        note("F8 is the default. Controller is unbound until recorded. Hold does not repeat; release after menus or recovery before pressing again.");
        note("Use while riding or airborne. No Bail, Noclip, Park Editor, First person and online play block manual bails.");
        if (!value.bail_controls_save_status.empty()) note(value.bail_controls_save_status.c_str());
        end_card();
        begin_card(menu,"slam-xray","X-RAY");
        auto visuals=value.visuals;
        bool changed=false;
        ImGui::BeginDisabled(!value.visual_options_ready);
        if (toggle_row(menu,"X-ray in normal play","Keep X-ray active without attempts or the Slam score HUD.",visuals.normal_play,value.visual_options_ready)) changed=true;
        const auto visibility_label=[&](dingosdk::slam::XrayVisibility mode) {
            return visuals.normal_play && mode==dingosdk::slam::XrayVisibility::attempt ? "Always" : dingosdk::slam::xray_visibility_name(mode);
        };
        field(menu,"Show skeleton");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##slam-xray-visibility",visibility_label(visuals.visibility))) {
            for (unsigned i=0;i<static_cast<unsigned>(dingosdk::slam::XrayVisibility::count);++i) {
                const auto mode=static_cast<dingosdk::slam::XrayVisibility>(i);
                if (ImGui::Selectable(visibility_label(mode),visuals.visibility==mode)) {
                    visuals.visibility=mode; changed=true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(visuals.visibility==dingosdk::slam::XrayVisibility::off);
        field(menu,"Opacity"); ImGui::SetNextItemWidth(-FLT_MIN);
        changed|=ImGui::SliderFloat("##slam-xray-opacity",&visuals.opacity,.1f,1.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        if (toggle_row(menu,"Only impacted bones","Reveal the bones involved in hard contacts; hide untouched bones.",visuals.only_impacted,value.visual_options_ready)) changed=true;
        if (toggle_row(menu,"Reduced effects","Keep steady injury colors; disable flashes, camera impacts and slow motion.",visuals.reduced_effects,value.visual_options_ready)) changed=true;
        if (toggle_row(menu,"Fracture marks","Show a persistent jagged crack on fractured bones.",visuals.fracture_marks,value.visual_options_ready)) changed=true;
        if (toggle_row(menu,"Impact sound","Add a thud for contacts and a crunch when a bone fractures.",visuals.impact_sound,value.visual_options_ready)) changed=true;
        ImGui::BeginDisabled(!visuals.impact_sound);
        field(menu,"Impact sound volume"); ImGui::SetNextItemWidth(-FLT_MIN);
        changed|=ImGui::SliderFloat("##slam-sound-volume",&visuals.sound_volume,0.f,1.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(visuals.reduced_effects);
        if (toggle_row(menu,"Impact camera","Briefly punch in and shake the gameplay camera on severe hits or fractures.",visuals.impact_camera,value.visual_options_ready)) changed=true;
        ImGui::BeginDisabled(!visuals.impact_camera);
        field(menu,"Camera impact strength"); ImGui::SetNextItemWidth(-FLT_MIN);
        changed|=ImGui::SliderFloat("##slam-camera-strength",&visuals.impact_camera_strength,0.f,1.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        if (toggle_row(menu,"Impact slow motion","Briefly slow severe hits and new fractures, then restore your game speed.",visuals.slow_motion,value.visual_options_ready)) changed=true;
        ImGui::BeginDisabled(!visuals.slow_motion);
        field(menu,"Game speed on impact"); ImGui::SetNextItemWidth(-FLT_MIN);
        int impact_speed=static_cast<int>(std::lround(visuals.slow_motion_scale*100));
        if (ImGui::SliderInt("##slam-impact-speed",&impact_speed,10,100,"%d%%",ImGuiSliderFlags_AlwaysClamp)) {
            visuals.slow_motion_scale=static_cast<float>(impact_speed)/100; changed=true;
        }
        field(menu,"Slow-motion duration"); ImGui::SetNextItemWidth(-FLT_MIN);
        changed|=ImGui::SliderFloat("##slam-slow-motion-seconds",&visuals.slow_motion_seconds,.15f,1.f,"%.2f seconds",ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        field(menu,"Impact flash strength"); ImGui::SetNextItemWidth(-FLT_MIN);
        changed|=ImGui::SliderFloat("##slam-xray-flash",&visuals.flash_strength,0.f,1.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        if (visuals.visibility==dingosdk::slam::XrayVisibility::impact) {
            field(menu,"Visible after impact"); ImGui::SetNextItemWidth(-FLT_MIN);
            changed|=ImGui::SliderFloat("##slam-xray-seconds",&visuals.impact_duration_s,.3f,5.f,"%.1f seconds",ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::EndDisabled();
        if (ImGui::Button("Reset X-ray settings",ImVec2(-FLT_MIN,0))) {visuals={}; changed=true;}
        ImGui::EndDisabled();
        if (changed) (void)dingosdk::slam::set_visual_options(visuals);
        if (visuals.normal_play) note("Normal-play X-ray follows every fall and clears injury highlights after recovery. No attempt or reset is needed.");
        if (value.first_person) note("The skeleton is hidden while First person is enabled.");
        note("Blue: uninjured. Orange: bruised. Red: fractured. X-ray settings are saved automatically.");
        if (!value.visual_save_status.empty()) note(value.visual_save_status.c_str());
        if (visuals.impact_sound && !value.impact_audio_status.empty()) note(value.impact_audio_status.c_str());
        if (visuals.reduced_effects) note("Reduced effects suppresses bright pulses, camera impacts and automatic slow motion.");
        else if (visuals.slow_motion && !value.slow_motion_status.empty()) note(value.slow_motion_status.c_str());
        if (visuals.impact_camera && !visuals.reduced_effects && !value.impact_camera_available) note("Impact camera is unavailable in the current view.");
        end_card();
        if (ImGui::TreeNode("Telemetry")) {
            ImGui::Text("Samples: %llu  |  Rejected: %llu", static_cast<unsigned long long>(value.samples), static_cast<unsigned long long>(value.dropped));
            ImGui::Text("Contacts: %u  |  Physics state: %u", value.contacts, value.physics_state);
            ImGui::Text("Manual bails: %llu queued | %llu selected | %llu published",
                static_cast<unsigned long long>(value.manual_bails_queued),static_cast<unsigned long long>(value.manual_bails_selected),
                static_cast<unsigned long long>(value.manual_bails_published));
            ImGui::Text("Rendered poses: %llu  |  Rejected: %llu", static_cast<unsigned long long>(value.rendered_poses),
                static_cast<unsigned long long>(value.rejected_poses));
            ImGui::Text("Render exports: %llu  |  Evaluations: %llu  |  Superseded: %llu",
                static_cast<unsigned long long>(value.export_poses),static_cast<unsigned long long>(value.animation_poses),
                static_cast<unsigned long long>(value.superseded_poses));
            note(value.availability.c_str());
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    ImGui::PopID();
}
}
