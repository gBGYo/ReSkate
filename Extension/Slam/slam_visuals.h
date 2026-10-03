#pragma once
#include "slam_model.h"
#include <optional>
#include <string_view>

namespace dingosdk::slam {
enum class XrayVisibility : unsigned { attempt, bail, impact, off, count };
const char* xray_visibility_name(XrayVisibility value) noexcept;
struct VisualOptions {
    XrayVisibility visibility = XrayVisibility::attempt;
    float opacity = .86f, flash_strength = .35f, impact_duration_s = 1.5f;
    bool reduced_effects{}, only_impacted{}, normal_play{};
    bool operator==(const VisualOptions&) const = default;
};
bool valid_visual_options(const VisualOptions& value) noexcept;
std::string encode_visual_options(const VisualOptions& value);
std::optional<VisualOptions> decode_visual_options(std::string_view text) noexcept;
struct VisualEvents {
    std::array<std::uint64_t,injury_bone_count> impacts_at_ms{};
    std::array<float,injury_bone_count> severity{};
    std::uint64_t latest_impact_ms{};
    unsigned observed_impacts{};
    void observe(const Result& result,std::uint64_t now) noexcept;
    void reset() noexcept { *this={}; }
};
struct VisualAppearance {
    std::array<std::array<float,4>,injury_bone_count> colors{};
    float opacity{};
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
