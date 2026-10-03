#include "slam_visuals.h"
#include "Engine/Core/Json/json.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dingosdk::slam {
const char* xray_visibility_name(XrayVisibility value) noexcept {
    switch (value) {
    case XrayVisibility::attempt: return "During attempts";
    case XrayVisibility::bail: return "After bailing";
    case XrayVisibility::impact: return "After impacts";
    case XrayVisibility::off: return "Off";
    default: return "Unknown";
    }
}
bool valid_visual_options(const VisualOptions& v) noexcept {
    return v.visibility<XrayVisibility::count && std::isfinite(v.opacity) && v.opacity>=.1f && v.opacity<=1 &&
        std::isfinite(v.flash_strength) && v.flash_strength>=0 && v.flash_strength<=1 &&
        std::isfinite(v.impact_duration_s) && v.impact_duration_s>=.3f && v.impact_duration_s<=5;
}
std::string encode_visual_options(const VisualOptions& v) {
    if (!valid_visual_options(v)) throw std::invalid_argument("Invalid Slam visual options");
    return Json{{"version",1},{"visibility",static_cast<unsigned>(v.visibility)},{"opacity",v.opacity},
        {"flash",v.flash_strength},{"impactSeconds",v.impact_duration_s},{"reduced",v.reduced_effects},
        {"onlyImpacted",v.only_impacted},{"normalPlay",v.normal_play}}.dump();
}
std::optional<VisualOptions> decode_visual_options(std::string_view text) noexcept {
    try {
        const auto doc=Json::parse(text,{512,4,32});
        if (!doc.is_object() || !doc.contains("version") || !doc.at("version").is_number_integer() ||
            doc.at("version").get<std::int64_t>()!=1 || !doc.contains("visibility") ||
            !doc.at("visibility").is_number_integer() || !doc.contains("opacity") ||
            !doc.contains("flash") || !doc.contains("impactSeconds") || !doc.contains("reduced") ||
            !doc.at("reduced").is_boolean()) return {};
        const auto visibility=doc.at("visibility").get<std::int64_t>();
        if (visibility<0 || visibility>=static_cast<std::int64_t>(XrayVisibility::count)) return {};
        VisualOptions v;
        v.visibility=static_cast<XrayVisibility>(visibility); v.reduced_effects=doc.at("reduced").get<bool>();
        // Existing v1 saves predate the optional contact-only display.
        if (doc.contains("onlyImpacted")) {
            if (!doc.at("onlyImpacted").is_boolean()) return {};
            v.only_impacted=doc.at("onlyImpacted").get<bool>();
        }
        if (doc.contains("normalPlay")) {
            if (!doc.at("normalPlay").is_boolean()) return {};
            v.normal_play=doc.at("normalPlay").get<bool>();
        }
        const std::array<std::pair<std::string_view,float*>,3> numbers{{
            {"opacity",&v.opacity},{"flash",&v.flash_strength},{"impactSeconds",&v.impact_duration_s}}};
        for (const auto& [name,output] : numbers) {
            if (!doc.at(name).is_number()) return {};
            const auto number=doc.at(name).get<double>();
            if (!std::isfinite(number) || number<0 || number>5) return {};
            *output=static_cast<float>(number);
        }
        return valid_visual_options(v) ? std::optional(v) : std::nullopt;
    } catch (...) { return {}; }
}
void VisualEvents::observe(const Result& result,std::uint64_t now) noexcept {
    if (result.cancelled || result.phase==Phase::ready) {reset(); return;}
    if (result.impacts<observed_impacts) reset();
    if (result.impacts>observed_impacts) {
        for (std::size_t i=0;i<injury_bone_count;++i) {
            if (result.bone_injuries[i].severity>severity[i]) impacts_at_ms[i]=now;
        }
        latest_impact_ms=now;
    }
    observed_impacts=result.impacts;
    for (std::size_t i=0;i<injury_bone_count;++i) severity[i]=result.bone_injuries[i].severity;
}
void FreeplayXray::step(const Frame& frame,std::uint64_t now) {
    if (!challenge_.running() && frame.valid && !frame.bailed) {
        if (challenge_.begin(frame)) events_.reset();
    } else challenge_.step(frame);
    if (challenge_.result().cancelled) reset();
    events_.observe(challenge_.result(),now);
}
namespace {
float age(std::uint64_t at,std::uint64_t now) noexcept {
    return at && now>=at ? static_cast<float>(now-at)/1000.f : 1000000.f;
}
float smooth_fade(float remaining) noexcept {
    const float value=std::clamp(remaining,0.f,1.f);
    return value*value*(3-2*value);
}
}
VisualAppearance visual_appearance(const VisualOptions& options,const Result& result,
    const VisualEvents& events,std::uint64_t now,bool first_person,bool normal_play) noexcept {
    VisualAppearance next;
    if (!valid_visual_options(options) || first_person || result.cancelled || (!normal_play && result.phase==Phase::ready) ||
        options.visibility==XrayVisibility::off) return next;
    const bool bailed=result.phase==Phase::bailed || result.phase==Phase::settled || result.phase==Phase::results;
    if (options.visibility==XrayVisibility::bail && !bailed) return next;
    next.opacity=options.opacity;
    if (options.visibility==XrayVisibility::impact) {
        const float elapsed=age(events.latest_impact_ms,now);
        next.opacity*=smooth_fade((options.impact_duration_s-elapsed)/(options.impact_duration_s*.4f));
    }
    for (std::size_t i=0;i<injury_bone_count;++i) {
        const auto& injury=result.bone_injuries[i];
        auto& color=next.colors[i];
        color=injury.fractured ? std::array<float,4>{1,.16f,.22f,next.opacity} :
            injury.severity>0 ? std::array<float,4>{1,.55f,.15f,next.opacity} : std::array<float,4>{.72f,.9f,1,next.opacity};
        if (options.only_impacted && injury.severity<=0) color[3]=0;
        // One gentle tint on the affected bones, fading once rather than
        // cycling. Reduced effects retains the steady injury colors.
        const float flash=options.reduced_effects ? 0 : options.flash_strength*
            smooth_fade(1-age(events.impacts_at_ms[i],now)/.65f)*.5f;
        for (std::size_t axis=0;axis<3;++axis) color[axis]+=(1-color[axis])*flash;
    }
    return next;
}
}
