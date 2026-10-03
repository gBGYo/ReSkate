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
        std::isfinite(v.impact_duration_s) && v.impact_duration_s>=.3f && v.impact_duration_s<=5 &&
        std::isfinite(v.sound_volume) && v.sound_volume>=0 && v.sound_volume<=1 &&
        std::isfinite(v.slow_motion_scale) && v.slow_motion_scale>=.1f && v.slow_motion_scale<=1 &&
        std::isfinite(v.slow_motion_seconds) && v.slow_motion_seconds>=.15f && v.slow_motion_seconds<=1 &&
        std::isfinite(v.impact_camera_strength) && v.impact_camera_strength>=0 && v.impact_camera_strength<=1;
}
std::string encode_visual_options(const VisualOptions& v) {
    if (!valid_visual_options(v)) throw std::invalid_argument("Invalid Slam visual options");
    return Json{{"version",1},{"visibility",static_cast<unsigned>(v.visibility)},{"opacity",v.opacity},
        {"flash",v.flash_strength},{"impactSeconds",v.impact_duration_s},{"reduced",v.reduced_effects},
        {"onlyImpacted",v.only_impacted},{"normalPlay",v.normal_play},{"fractureMarks",v.fracture_marks},
        {"impactSound",v.impact_sound},{"soundVolume",v.sound_volume},{"slowMotion",v.slow_motion},
        {"slowMotionScale",v.slow_motion_scale},{"slowMotionSeconds",v.slow_motion_seconds},
        {"impactCamera",v.impact_camera},{"cameraStrength",v.impact_camera_strength}}.dump();
}
std::optional<VisualOptions> decode_visual_options(std::string_view text) noexcept {
    try {
        const auto doc=Json::parse(text,{512,4,48});
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
        for (const auto& [name,field] : std::array<std::pair<std::string_view,bool*>,4>{{
            {"fractureMarks",&v.fracture_marks},{"impactSound",&v.impact_sound},{"slowMotion",&v.slow_motion},
            {"impactCamera",&v.impact_camera}}}) {
            if (doc.contains(name)) {
                if (!doc.at(name).is_boolean()) return {};
                *field=doc.at(name).get<bool>();
            }
        }
        if (doc.contains("soundVolume")) {
            if (!doc.at("soundVolume").is_number()) return {};
            const auto value=doc.at("soundVolume").get<double>();
            if (!std::isfinite(value) || value<0 || value>1) return {};
            v.sound_volume=static_cast<float>(value);
        }
        const std::array<std::pair<std::string_view,float*>,6> numbers{{
            {"opacity",&v.opacity},{"flash",&v.flash_strength},{"impactSeconds",&v.impact_duration_s},
            {"slowMotionScale",&v.slow_motion_scale},{"slowMotionSeconds",&v.slow_motion_seconds},
            {"cameraStrength",&v.impact_camera_strength}}};
        for (const auto& [name,output] : numbers) {
            if (!doc.contains(name) && (name=="slowMotionScale" || name=="slowMotionSeconds" || name=="cameraStrength")) continue;
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
        latest_severity=0; latest_fracture=false;
        for (std::size_t i=0;i<injury_bone_count;++i) {
            const auto change=result.bone_injuries[i].severity-severity[i];
            if (change>0) {
                impacts_at_ms[i]=now;
                const bool broke=result.bone_injuries[i].fractured && !fractured[i];
                if (broke) fractures_at_ms[i]=now;
                if ((broke && !latest_fracture) || (broke==latest_fracture && change>latest_severity)) {
                    latest_bone=static_cast<unsigned>(i); latest_severity=change; latest_fracture=broke;
                }
            }
        }
        latest_impact_ms=now;
    }
    observed_impacts=result.impacts;
    for (std::size_t i=0;i<injury_bone_count;++i) {
        severity[i]=result.bone_injuries[i].severity;
        fractured[i]=result.bone_injuries[i].fractured;
    }
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
        next.damage[i][0]=options.fracture_marks && injury.fractured ? 1.f : 0.f;
        next.damage[i][1]=options.reduced_effects ? 0 : options.flash_strength*
            smooth_fade(1-age(events.impacts_at_ms[i],now)/.16f);
        next.damage[i][2]=injury.fractured ? 1.f : injury.severity>0 ? .8f : .35f;
        next.damage[i][3]=options.fracture_marks && injury.fractured && !options.reduced_effects ?
            smooth_fade(1-age(events.fractures_at_ms[i],now)/.35f) : 0;
    }
    return next;
}
std::vector<std::int16_t> make_impact_sound(bool fracture) {
    constexpr unsigned rate=48000, count=9600;
    std::vector<std::int16_t> samples(count);
    std::uint32_t random=fracture ? 0x93a571u : 0x283d71u;
    float previous_noise{};
    for (unsigned i=0;i<count;++i) {
        const float t=static_cast<float>(i)/rate;
        random^=random<<13; random^=random>>17; random^=random<<5;
        const float noise=static_cast<float>(random&65535u)/32767.5f-1;
        const float high=(noise-previous_noise)*.5f; previous_noise=noise;
        float crack{};
        const unsigned bursts=fracture ? 4u : 1u;
        for (unsigned j=0;j<bursts;++j) {
            const float since=t-static_cast<float>(j)*.017f;
            if (since>=0) crack+=high*std::exp(-since/(fracture ? .009f : .013f))*(1-static_cast<float>(j)*.15f);
        }
        const float thud=std::sin(t*6.28318530718f*(fracture ? 72.f : 88.f))*std::exp(-t/.04f);
        const float attack=std::min(1.f,t/.0004f);
        const float end=std::clamp((.2f-t)/.015f,0.f,1.f);
        const float value=(crack*(fracture ? .68f : .4f)+thud*.38f)*attack*end;
        samples[i]=static_cast<std::int16_t>(std::lround(std::clamp(value,-.8f,.8f)*32767));
    }
    samples.front()=samples.back()=0;
    return samples;
}
TimePulseFrame SlowMotionPulse::step(const VisualOptions& options,const VisualEvents& events,std::uint64_t now,bool allowed) noexcept {
    const bool new_hit=events.latest_impact_ms && events.latest_impact_ms!=observed_ms_;
    observed_ms_=events.latest_impact_ms;
    if (!allowed || !events.latest_impact_ms || !valid_visual_options(options) || !options.slow_motion || options.reduced_effects) {
        cancel(); return {};
    }
    if (until_ && now>=until_) cancel();
    bool started{};
    if (new_hit && now>=events.latest_impact_ms && now-events.latest_impact_ms<=150 &&
        (events.latest_fracture || events.latest_severity>=225) && !until_ &&
        (!last_started_ || now-last_started_>=500) && options.slow_motion_scale<1) {
        began_=now; until_=now+static_cast<std::uint64_t>(options.slow_motion_seconds*1000);
        last_started_=now; scale_=options.slow_motion_scale; started=true;
    }
    if (!until_ || now<began_) {cancel(); return {};}
    const float progress=static_cast<float>(now-began_)/static_cast<float>(until_-began_);
    // Brief hold, then a smooth return to the prior speed. Further floor
    // contacts cannot stretch the same pulse into prolonged slow motion.
    const float recovery=std::clamp((progress-.2f)/.8f,0.f,1.f);
    const float smooth=recovery*recovery*(3-2*recovery);
    return {scale_+(1-scale_)*smooth,true,started};
}
CameraMotion sample_camera_impulse(const CameraImpulse& impulse,std::uint64_t now) noexcept {
    if (!impulse.started_ms || now<impulse.started_ms || now-impulse.started_ms>=450 ||
        !std::isfinite(impulse.strength) || impulse.strength<=0 || impulse.strength>1) return {};
    const float t=static_cast<float>(now-impulse.started_ms)/1000;
    const float envelope=std::min(1.f,t/.02f)*std::exp(-t/.12f)*smooth_fade((.45f-t)/.12f)*impulse.strength;
    CameraMotion result;
    result.active=true;
    result.translation={.045f*envelope*std::sin(t*83),.032f*envelope*std::sin(t*67),-.025f*envelope};
    result.roll=.012f*envelope*std::sin(t*71);
    result.fov_scale=1-.12f*impulse.strength*std::sin(t/.45f*3.14159265f);
    return result;
}
CameraImpulse ImpactCameraPulse::step(const VisualOptions& options,const VisualEvents& events,std::uint64_t now,bool allowed) noexcept {
    const bool new_hit=events.latest_impact_ms && events.latest_impact_ms!=observed_ms_;
    observed_ms_=events.latest_impact_ms;
    if (!allowed || !events.latest_impact_ms || !valid_visual_options(options) || options.reduced_effects ||
        !options.impact_camera || options.impact_camera_strength<=0) {impulse_={}; return {};}
    if (impulse_.started_ms && (now<impulse_.started_ms || now-impulse_.started_ms>=450)) impulse_={};
    if (!impulse_.started_ms && new_hit && now>=events.latest_impact_ms && now-events.latest_impact_ms<=150 &&
        (events.latest_fracture || events.latest_severity>=225) && (!last_started_ || now-last_started_>=500)) {
        impulse_={now,options.impact_camera_strength*std::clamp(std::sqrt(std::max(0.f,events.latest_severity))/30,.5f,1.f)};
        last_started_=now;
    }
    return impulse_;
}
}
