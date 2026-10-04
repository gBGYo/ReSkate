#include "slam_visuals.h"
#include "Engine/Core/Json/json.h"
#include <algorithm>
#include <cmath>
#include <cstring>
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
        std::isfinite(v.slow_motion_seconds) && v.slow_motion_seconds>=.15f && v.slow_motion_seconds<=3 &&
        std::isfinite(v.slow_motion_hold) && v.slow_motion_hold>=.1f && v.slow_motion_hold<=.8f &&
        std::isfinite(v.effect_severity) && v.effect_severity>=25 && v.effect_severity<=2000 &&
        std::isfinite(v.fracture_volume) && v.fracture_volume>=0 && v.fracture_volume<=1 &&
        v.fracture_sound_path.size()<=240 && v.fracture_sound_path.find('\0')==std::string::npos &&
        std::isfinite(v.player_zoom_strength) && v.player_zoom_strength>=0 && v.player_zoom_strength<=.4f &&
        std::isfinite(v.player_zoom_ease_seconds) && v.player_zoom_ease_seconds>=.08f && v.player_zoom_ease_seconds<=.6f &&
        std::isfinite(v.player_follow_seconds) && v.player_follow_seconds>=.04f && v.player_follow_seconds<=.4f &&
        std::isfinite(v.pass_out_strength) && v.pass_out_strength>=0 && v.pass_out_strength<=1 &&
        std::isfinite(v.pass_out_seconds) && v.pass_out_seconds>=.5f && v.pass_out_seconds<=4 &&
        std::isfinite(v.pass_out_severity) && v.pass_out_severity>=25 && v.pass_out_severity<=2000 &&
        std::isfinite(v.pass_out_redness) && v.pass_out_redness>=0 && v.pass_out_redness<=1 &&
        std::isfinite(v.impact_camera_strength) && v.impact_camera_strength>=0 && v.impact_camera_strength<=1;
}
std::string encode_visual_options(const VisualOptions& v) {
    if (!valid_visual_options(v)) throw std::invalid_argument("Invalid Slam visual options");
    return Json{{"version",1},{"visibility",static_cast<unsigned>(v.visibility)},{"opacity",v.opacity},
        {"flash",v.flash_strength},{"impactSeconds",v.impact_duration_s},{"reduced",v.reduced_effects},
        {"onlyImpacted",v.only_impacted},{"onlyFractured",v.only_fractured},{"normalPlay",v.normal_play},{"fractureMarks",v.fracture_marks},
        {"impactSound",v.impact_sound},{"soundVolume",v.sound_volume},{"slowMotion",v.slow_motion},
        {"slowMotionScale",v.slow_motion_scale},{"slowMotionSeconds",v.slow_motion_seconds},
        {"impactCamera",v.impact_camera},{"cameraStrength",v.impact_camera_strength},
        {"fractureSound",v.fracture_sound},{"fractureVolume",v.fracture_volume},{"fractureSoundPath",v.fracture_sound_path},
        {"slowMotionHold",v.slow_motion_hold},{"effectSeverity",v.effect_severity},
        {"playerZoom",v.player_zoom},{"playerZoomStrength",v.player_zoom_strength},
        {"playerZoomEaseSeconds",v.player_zoom_ease_seconds},{"playerFollowSeconds",v.player_follow_seconds},
        {"passOut",v.pass_out},{"passOutHud",v.pass_out_hud},{"passOutStrength",v.pass_out_strength},
        {"passOutSeconds",v.pass_out_seconds},{"passOutSeverity",v.pass_out_severity},{"passOutRedness",v.pass_out_redness}}.dump();
}
std::optional<VisualOptions> decode_visual_options(std::string_view text) noexcept {
    try {
        const auto doc=Json::parse(text,{4096,4,96});
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
        for (const auto& [name,field] : std::array<std::pair<std::string_view,bool*>,9>{{
            {"fractureMarks",&v.fracture_marks},{"impactSound",&v.impact_sound},{"slowMotion",&v.slow_motion},
            {"impactCamera",&v.impact_camera},{"fractureSound",&v.fracture_sound},{"playerZoom",&v.player_zoom},
            {"passOut",&v.pass_out},{"passOutHud",&v.pass_out_hud},{"onlyFractured",&v.only_fractured}}}) {
            if (doc.contains(name)) {
                if (!doc.at(name).is_boolean()) return {};
                *field=doc.at(name).get<bool>();
            }
        }
        if (doc.contains("fractureSoundPath")) {
            if (!doc.at("fractureSoundPath").is_string()) return {};
            v.fracture_sound_path=doc.at("fractureSoundPath").get<std::string>();
        }
        if (doc.contains("soundVolume")) {
            if (!doc.at("soundVolume").is_number()) return {};
            const auto value=doc.at("soundVolume").get<double>();
            if (!std::isfinite(value) || value<0 || value>1) return {};
            v.sound_volume=static_cast<float>(value);
        }
        // Preserve a previous master sound-off preference when upgrading.
        if (!doc.contains("fractureSound") && doc.contains("impactSound")) v.fracture_sound=v.impact_sound;
        if (!doc.contains("fractureVolume") && doc.contains("soundVolume")) v.fracture_volume=v.sound_volume;
        const std::array<std::pair<std::string_view,float*>,16> numbers{{
            {"opacity",&v.opacity},{"flash",&v.flash_strength},{"impactSeconds",&v.impact_duration_s},
            {"slowMotionScale",&v.slow_motion_scale},{"slowMotionSeconds",&v.slow_motion_seconds},
            {"cameraStrength",&v.impact_camera_strength},{"fractureVolume",&v.fracture_volume},
            {"slowMotionHold",&v.slow_motion_hold},{"effectSeverity",&v.effect_severity},
            {"playerZoomStrength",&v.player_zoom_strength},{"passOutStrength",&v.pass_out_strength},
            {"passOutSeconds",&v.pass_out_seconds},{"passOutSeverity",&v.pass_out_severity},
            {"playerZoomEaseSeconds",&v.player_zoom_ease_seconds},{"playerFollowSeconds",&v.player_follow_seconds},
            {"passOutRedness",&v.pass_out_redness}}};
        for (const auto& [name,output] : numbers) {
            if (!doc.contains(name) && name!="opacity" && name!="flash" && name!="impactSeconds") continue;
            if (!doc.at(name).is_number()) return {};
            const auto number=doc.at(name).get<double>();
            if (!std::isfinite(number) || number<0 || number>2000) return {};
            *output=static_cast<float>(number);
        }
        return valid_visual_options(v) ? std::optional(v) : std::nullopt;
    } catch (...) { return {}; }
}
void VisualEvents::observe(const Result& result,std::uint64_t now) noexcept {
    if (result.cancelled || result.phase==Phase::ready) {reset(); return;}
    if (result.impacts<observed_impacts) reset();
    // Bone injuries also change during a region's score cooldown. Observe
    // their deltas directly instead of using the scored-impact counter.
    float newest_severity{};
    bool newest_fracture{},changed{};
    unsigned newest_bone{};
    for (std::size_t i=0;i<injury_bone_count;++i) {
        const auto change=result.bone_injuries[i].severity-severity[i];
        if (change>0) {
            changed=true; impacts_at_ms[i]=now;
            const bool broke=result.bone_injuries[i].fractured && !fractured[i];
            if (broke) fractures_at_ms[i]=now;
            if ((broke && !newest_fracture) || (broke==newest_fracture && change>newest_severity)) {
                newest_bone=static_cast<unsigned>(i); newest_severity=change; newest_fracture=broke;
            }
        }
    }
    if (changed) {
        latest_impact_ms=now; latest_bone=newest_bone;
        latest_severity=newest_severity; latest_fracture=newest_fracture;
    }
    observed_impacts=result.impacts;
    for (std::size_t i=0;i<injury_bone_count;++i) {
        severity[i]=result.bone_injuries[i].severity;
        fractured[i]=result.bone_injuries[i].fractured;
    }
}
void FreeplayXray::step(const Frame& frame,std::uint64_t now,const ScoreRules& rules) {
    const bool new_rules=challenge_.result().phase==Phase::attempt && challenge_.result().config.scoring!=rules;
    if ((!challenge_.running() || new_rules) && frame.valid && !frame.bailed) {
        Config config; config.scoring=rules;
        if (challenge_.begin(frame,config)) events_.reset();
    } else challenge_.step(frame);
    if (challenge_.result().cancelled) reset();
    events_.observe(challenge_.result(),now);
    if (events_.latest_impact_ms) {
        impact_result_=challenge_.result();
        impact_events_=events_;
    }
}
namespace {
float age(std::uint64_t at,std::uint64_t now) noexcept {
    return at && now>=at ? static_cast<float>(now-at)/1000.f : 1000000.f;
}
float smooth_fade(float remaining) noexcept {
    const float value=std::clamp(remaining,0.f,1.f);
    return value*value*(3-2*value);
}
float smoother_fade(float remaining) noexcept {
    const float value=std::clamp(remaining,0.f,1.f);
    return std::clamp(value*value*value*(value*(value*6-15)+10),0.f,1.f);
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
        const bool bruised=is_bruised(injury,result.config.scoring);
        const bool injured=injury.fractured || bruised;
        auto& color=next.colors[i];
        color=injury.fractured ? std::array<float,4>{1,.16f,.22f,next.opacity} :
            bruised ? std::array<float,4>{1,.55f,.15f,next.opacity} : std::array<float,4>{.72f,.9f,1,next.opacity};
        const bool hidden=options.only_fractured ? !injury.fractured : options.only_impacted && !injured;
        if (hidden) color[3]=0;
        // One gentle tint on the affected bones, fading once rather than
        // cycling. Reduced effects retains the steady injury colors.
        const float flash=options.reduced_effects || !injured ? 0 : options.flash_strength*
            smooth_fade(1-age(events.impacts_at_ms[i],now)/.65f)*.5f;
        for (std::size_t axis=0;axis<3;++axis) color[axis]+=(1-color[axis])*flash;
        next.damage[i][0]=options.fracture_marks && injury.fractured ? 1.f : 0.f;
        next.damage[i][1]=options.reduced_effects || !injured ? 0 : options.flash_strength*
            smooth_fade(1-age(events.impacts_at_ms[i],now)/.16f);
        next.damage[i][2]=injury.fractured ? 1.f : bruised ? .8f : .35f;
        next.damage[i][3]=options.fracture_marks && injury.fractured && !options.reduced_effects ?
            smooth_fade(1-age(events.fractures_at_ms[i],now)/.35f) : 0;
    }
    return next;
}
std::vector<std::int16_t> make_impact_sound(bool fracture,unsigned variant) {
    constexpr unsigned rate=48000, count=9600;
    std::vector<std::int16_t> samples(count);
    std::uint32_t random=(fracture ? 0x93a571u : 0x283d71u)^((variant+1)*0x9e3779b9u);
    float previous_noise{},low_noise{};
    const float variation=static_cast<float>(variant%4);
    for (unsigned i=0;i<count;++i) {
        const float t=static_cast<float>(i)/rate;
        random^=random<<13; random^=random>>17; random^=random<<5;
        const float noise=static_cast<float>(random&65535u)/32767.5f-1;
        const float high=(noise-previous_noise)*.5f; previous_noise=noise;
        low_noise+=.18f*(noise-low_noise);
        float crack{};
        const unsigned bursts=fracture ? 7u : 1u;
        for (unsigned j=0;j<bursts;++j) {
            const float since=t-static_cast<float>(j*j)*(.0013f+variation*.0001f);
            if (since>=0) {
                const float texture=fracture ? high*.55f+(noise-low_noise)*.45f : high;
                crack+=texture*std::exp(-since/(fracture ? .0035f : .013f))/(1+static_cast<float>(j)*.35f);
            }
        }
        const float thud=std::sin(t*6.28318530718f*(fracture ? 72.f : 88.f))*std::exp(-t/.04f);
        const float attack=std::min(1.f,t/.0004f);
        const float end=std::clamp((.2f-t)/.015f,0.f,1.f);
        // Dry splinter transients over a short, gritty body layer. Irregular
        // spacing and seeded variants avoid a repeating pitched noise burst.
        const float grit=fracture ? low_noise*.65f*std::exp(-t/.038f) : 0;
        const float value=(crack*(fracture ? .8f : .4f)+thud*(fracture ? .2f : .38f)+grit)*attack*end;
        samples[i]=static_cast<std::int16_t>(std::lround(std::clamp(value,-.8f,.8f)*32767));
    }
    samples.front()=samples.back()=0;
    return samples;
}
std::optional<std::vector<std::int16_t>> decode_impact_wav(std::span<const std::byte> bytes) noexcept {
    try {
        if (bytes.size()<44 || bytes.size()>256*1024) return {};
        const auto tag=[&](std::size_t at,const char* name) {return std::memcmp(bytes.data()+at,name,4)==0;};
        const auto word=[&](std::size_t at) {
            return std::to_integer<unsigned>(bytes[at])|(std::to_integer<unsigned>(bytes[at+1])<<8);
        };
        const auto dword=[&](std::size_t at) {
            return static_cast<std::uint32_t>(word(at))|(static_cast<std::uint32_t>(word(at+2))<<16);
        };
        if (!tag(0,"RIFF") || !tag(8,"WAVE") || dword(4)!=bytes.size()-8) return {};
        bool format{};
        std::span<const std::byte> data;
        for (std::size_t at=12;at<bytes.size();) {
            if (bytes.size()-at<8) return {};
            const auto size=static_cast<std::size_t>(dword(at+4));
            if (size>bytes.size()-at-8) return {};
            if (tag(at,"fmt ")) {
                if (format || size<16 || word(at+8)!=1 || word(at+10)!=1 || dword(at+12)!=48000 ||
                    dword(at+16)!=96000 || word(at+20)!=2 || word(at+22)!=16) return {};
                format=true;
            } else if (tag(at,"data")) {
                if (!data.empty() || !size || size%2 || size>192000) return {};
                data=bytes.subspan(at+8,size);
            }
            const auto padded=size+(size&1);
            if (padded>bytes.size()-at-8) return {};
            at+=8+padded;
        }
        if (!format || data.empty()) return {};
        std::vector<std::int16_t> samples(data.size()/2);
        std::memcpy(samples.data(),data.data(),data.size());
        return samples;
    } catch (...) {return {};}
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
        (events.latest_fracture || events.latest_severity>=options.effect_severity) && !until_ &&
        (!last_started_ || now-last_started_>=500) && options.slow_motion_scale<1) {
        began_=now; until_=now+static_cast<std::uint64_t>(options.slow_motion_seconds*1000);
        last_started_=now; scale_=options.slow_motion_scale; hold_=options.slow_motion_hold; started=true;
    }
    if (!until_ || now<began_) {cancel(); return {};}
    const float progress=static_cast<float>(now-began_)/static_cast<float>(until_-began_);
    // Brief hold, then a smooth return to the prior speed. Further floor
    // contacts cannot stretch the same pulse into prolonged slow motion.
    const float recovery=std::clamp((progress-hold_)/(1-hold_),0.f,1.f);
    const float smooth=recovery*recovery*(3-2*recovery);
    return {scale_+(1-scale_)*smooth,true,started};
}
CameraMotion sample_player_zoom(const ZoomImpulse& impulse,std::uint64_t now) noexcept {
    if (!impulse.started_ms || now<impulse.started_ms || !std::isfinite(impulse.seconds) || impulse.seconds<.15f || impulse.seconds>3 ||
        !std::isfinite(impulse.strength) || impulse.strength<=0 || impulse.strength>.4f ||
        !std::isfinite(impulse.ease_seconds) || impulse.ease_seconds<.08f || impulse.ease_seconds>.6f ||
        !std::isfinite(impulse.hold) || impulse.hold<.1f || impulse.hold>.8f) return {};
    const float elapsed=age(impulse.started_ms,now);
    if (elapsed>=impulse.seconds) return {};
    const float progress=elapsed/impulse.seconds;
    const float envelope=smoother_fade(elapsed/std::min(impulse.ease_seconds,impulse.seconds*.35f))*
        smoother_fade((1-progress)/(1-impulse.hold));
    CameraMotion motion;
    motion.active=true; motion.fov_scale=1-impulse.strength*envelope;
    motion.focus_weight=envelope;
    return motion;
}
Vec3 PlayerCameraFollow::step(const Vec3& target,std::uint64_t now,float seconds) noexcept {
    if (!std::all_of(target.begin(),target.end(),[](float value) {return std::isfinite(value) && std::abs(value)<1e6f;}) ||
        !std::isfinite(seconds) || seconds<.04f || seconds>.4f) {reset(); return {};}
    float distance{};
    for (std::size_t axis=0;axis<3;++axis) distance+=(target[axis]-position_[axis])*(target[axis]-position_[axis]);
    if (!ready_ || now<at_ || now-at_>250 || distance>100) {
        position_=target; velocity_={}; at_=now; ready_=true; return position_;
    }
    const float dt=static_cast<float>(now-at_)/1000;
    at_=now;
    const float omega=2/seconds,decay=std::exp(-omega*dt);
    for (std::size_t axis=0;axis<3;++axis) {
        const float displacement=position_[axis]-target[axis];
        const float movement=(velocity_[axis]+omega*displacement)*dt;
        position_[axis]=target[axis]+(displacement+movement)*decay;
        velocity_[axis]=(velocity_[axis]-omega*movement)*decay;
    }
    return position_;
}
PassOutFrame PassOutPulse::step(const VisualOptions& options,const VisualEvents& events,std::uint64_t now,bool allowed,bool bailed) noexcept {
    const bool new_hit=events.latest_impact_ms && events.latest_impact_ms!=observed_ms_;
    observed_ms_=events.latest_impact_ms;
    if (!bailed || !events.latest_impact_ms) {began_=0; triggered_=false; return {};}
    // Consume suppressed contacts too; reopening the menu cannot replay them.
    if (!allowed || !valid_visual_options(options) || !options.pass_out || options.reduced_effects) {
        began_=0; if (new_hit) triggered_=true; return {};
    }
    if (!triggered_ && new_hit && now>=events.latest_impact_ms && now-events.latest_impact_ms<=150 &&
        (events.latest_fracture || events.latest_severity>=options.pass_out_severity)) {
        triggered_=true; began_=now; seconds_=options.pass_out_seconds; strength_=options.pass_out_strength;
    }
    if (!began_ || now<began_) {began_=0; return {};}
    const float progress=age(began_,now)/seconds_;
    if (progress>=1) {began_=0; return {};}
    const float envelope=smooth_fade(progress/.25f)*smooth_fade((1-progress)/.5f)*strength_;
    return {envelope*.8f,envelope,options.pass_out_hud ? envelope : 0,options.pass_out_redness};
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
        (events.latest_fracture || events.latest_severity>=options.effect_severity) && (!last_started_ || now-last_started_>=500)) {
        impulse_={now,options.impact_camera_strength*std::clamp(std::sqrt(std::max(0.f,events.latest_severity))/30,.5f,1.f)};
        last_started_=now;
    }
    return impulse_;
}
}
