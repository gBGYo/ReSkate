#pragma once
#include "slam_model.h"
#include <optional>
#include <string_view>
#include <vector>

namespace dingosdk::slam {
enum class XrayVisibility : unsigned { attempt, bail, impact, off, count };
const char* xray_visibility_name(XrayVisibility value) noexcept;
struct VisualOptions {
    XrayVisibility visibility = XrayVisibility::attempt;
    float opacity = .86f, flash_strength = .35f, impact_duration_s = 1.5f;
    bool reduced_effects{}, only_impacted{}, normal_play{};
    bool fracture_marks = true, impact_sound = true;
    float sound_volume = .45f;
    bool slow_motion = true;
    float slow_motion_scale = .3f, slow_motion_seconds = .45f;
    bool impact_camera = true;
    float impact_camera_strength = .65f;
    bool operator==(const VisualOptions&) const = default;
};
bool valid_visual_options(const VisualOptions& value) noexcept;
std::string encode_visual_options(const VisualOptions& value);
std::optional<VisualOptions> decode_visual_options(std::string_view text) noexcept;
struct VisualEvents {
    std::array<std::uint64_t,injury_bone_count> impacts_at_ms{};
    std::array<std::uint64_t,injury_bone_count> fractures_at_ms{};
    std::array<float,injury_bone_count> severity{};
    std::uint64_t latest_impact_ms{};
    unsigned observed_impacts{};
    unsigned latest_bone{};
    float latest_severity{};
    bool latest_fracture{};
    std::array<bool,injury_bone_count> fractured{};
    void observe(const Result& result,std::uint64_t now) noexcept;
    void reset() noexcept { *this={}; }
};
struct VisualAppearance {
    std::array<std::array<float,4>,injury_bone_count> colors{};
    float opacity{};
    // x: fracture marks, y: hit pulse, z: injury tint, w: brief fracture opening.
    std::array<std::array<float,4>,injury_bone_count> damage{};
};
// Original, bounded 48 kHz mono PCM; no extracted or downloaded audio.
std::vector<std::int16_t> make_impact_sound(bool fracture);
struct TimePulseFrame {float factor = 1; bool active{},started{};};
class SlowMotionPulse {
public:
    TimePulseFrame step(const VisualOptions& options,const VisualEvents& events,std::uint64_t now,bool allowed) noexcept;
    void cancel() noexcept {began_=until_=0;}
private:
    std::uint64_t observed_ms_{},began_{},until_{},last_started_{};
    float scale_ = 1;
};
struct CameraImpulse {std::uint64_t started_ms{}; float strength{};};
struct CameraMotion {Vec3 translation{}; float roll{},fov_scale=1; bool active{};};
CameraMotion sample_camera_impulse(const CameraImpulse& impulse,std::uint64_t now) noexcept;
class ImpactCameraPulse {
public:
    CameraImpulse step(const VisualOptions& options,const VisualEvents& events,std::uint64_t now,bool allowed) noexcept;
private:
    CameraImpulse impulse_;
    std::uint64_t observed_ms_{},last_started_{};
};
// An independent, automatically rearmed fall tracker for normal skating.
// It never changes the player's Slam attempt or opens its score HUD.
class FreeplayXray {
public:
    void step(const Frame& frame, std::uint64_t now);
    void reset() { challenge_.reset(); events_.reset(); }
    const Result& result() const noexcept { return challenge_.result(); }
    const VisualEvents& events() const noexcept { return events_; }
private:
    Challenge challenge_;
    VisualEvents events_;
};
// Wall-clock envelopes keep fading after the result stops physics scoring.
// No native camera, frame rate, or GPU state enters this presentation model.
VisualAppearance visual_appearance(const VisualOptions& options,const Result& result,
    const VisualEvents& events,std::uint64_t now,bool first_person,bool normal_play=false) noexcept;
}
