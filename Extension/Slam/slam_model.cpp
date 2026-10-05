#include "slam_model.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace dingosdk::slam {
namespace {
float length(const Vec3& v) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }
bool finite(const Vec3& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x) && std::abs(x) < 1000000; });
}
Vec3 cross(const Vec3& a,const Vec3& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
float dot(const Vec3& a,const Vec3& b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool rigid_pose(const std::array<float,16>& pose) {
    const Vec3 x{pose[0],pose[1],pose[2]},y{pose[4],pose[5],pose[6]},z{pose[8],pose[9],pose[10]};
    return finite(x) && finite(y) && finite(z) && finite({pose[12],pose[13],pose[14]}) &&
        std::abs(dot(x,x)-1)<.05f && std::abs(dot(y,y)-1)<.05f && std::abs(dot(z,z)-1)<.05f &&
        std::abs(dot(x,y))<.05f && std::abs(dot(x,z))<.05f && std::abs(dot(y,z))<.05f && dot(cross(x,y),z)>.95f;
}
std::optional<Vec3> angular_velocity(const Body& before,const Body& body,float dt) {
    if (!before.pose_valid || !body.pose_valid || !rigid_pose(before.pose) || !rigid_pose(body.pose)) return {};
    Vec3 angular{};
    float trace{};
    for (std::size_t offset : {0u,4u,8u}) {
        const Vec3 a{before.pose[offset],before.pose[offset+1],before.pose[offset+2]};
        const Vec3 b{body.pose[offset],body.pose[offset+1],body.pose[offset+2]};
        const auto product=cross(a,b);
        for (std::size_t axis=0;axis<3;++axis) angular[axis]+=product[axis]/(2*dt);
        trace+=dot(a,b);
    }
    // sum(cross(old_axis,new_axis))/2dt estimates world angular velocity.
    // Large intersample turns are ambiguous: use linear telemetry instead.
    return trace>=2.f && finite(angular) && length(angular)<=100 ? std::optional(angular) : std::nullopt;
}
std::optional<Vec3> velocity_at_contact(const Body& body,const Vec3& angular,const Vec3& point) {
    Vec3 lever{};
    for (std::size_t axis=0;axis<3;++axis) lever[axis]=point[axis]-body.pose[12+axis];
    if (!finite(lever) || length(lever)>1.5f) return {};
    auto velocity=cross(angular,lever);
    for (std::size_t axis=0;axis<3;++axis) velocity[axis]+=body.velocity[axis];
    return finite(velocity) && length(velocity)<=300 ? std::optional(velocity) : std::nullopt;
}
bool usable(const Frame& frame) {
    if (!frame.valid || !frame.entity || !frame.world || !finite(frame.center) ||
        !frame.body_count || frame.body_count > max_bodies) return false;
    if (frame.board.identity && (!finite(frame.board.velocity) || length(frame.board.velocity)>300)) return false;
    if (frame.board.hard_landing && (!frame.board.identity || !finite(frame.board.normal) ||
        length(frame.board.normal)<.5f || length(frame.board.normal)>1.5f ||
        !std::isfinite(frame.board.speed) || frame.board.speed<0 || frame.board.speed>300)) return false;
    for (std::size_t i = 0; i < frame.body_count; ++i) {
        const auto& body = frame.bodies[i];
        if (body.region >= Region::count || body.joint< -1 || body.joint>=static_cast<int>(injury_bone_count) ||
            body.injury_joint< -1 || body.injury_joint>=static_cast<int>(injury_bone_count) ||
            !finite(body.velocity) || length(body.velocity) > 300 ||
            (body.speed_valid && (!std::isfinite(body.contact_speed) || body.contact_speed<0 || body.contact_speed>300)) ||
            (body.contact && body.normal_valid && (!finite(body.normal) || length(body.normal) < .5f || length(body.normal) > 1.5f)))
            return false;
    }
    return true;
}
}
const char* challenge_name(ChallengeKind kind) noexcept {
    constexpr const char* names[]{"Free slam", "Score target", "Impact chain", "Fracture target", "Big drop", "Hang time", "Long slide"};
    const auto i = static_cast<std::size_t>(kind);
    return i < std::size(names) ? names[i] : "Unknown";
}
const char* challenge_unit(ChallengeKind kind) noexcept {
    switch (kind) {
    case ChallengeKind::score: return "points";
    case ChallengeKind::chain: return "chained impacts";
    case ChallengeKind::fractures: return "fractured regions";
    case ChallengeKind::fall: case ChallengeKind::slide: return "metres";
    case ChallengeKind::airtime: return "seconds";
    default: return "";
    }
}
float default_target(ChallengeKind kind) noexcept {
    constexpr float targets[]{5000,5000,4,2,5,3,10};
    const auto i = static_cast<std::size_t>(kind);
    return i < std::size(targets) ? targets[i] : 5000;
}
bool valid_config(const Config& config) noexcept {
    if (config.kind >= ChallengeKind::count || !std::isfinite(config.target) || config.target < 1 || config.target > 1000000) return false;
    if (config.kind == ChallengeKind::fractures && config.target > region_count) return false;
    if ((config.kind == ChallengeKind::score || config.kind == ChallengeKind::chain || config.kind == ChallengeKind::fractures) &&
        std::floor(config.target) != config.target) return false;
    const auto& s = config.scoring;
    for (const auto value : {s.impact_rate,s.fracture_bonus,s.fall_rate,s.airtime_rate,s.slide_rate})
        if (!std::isfinite(value) || value < 0 || value > 10000) return false;
    return std::isfinite(s.chain_step) && s.chain_step >= 0 && s.chain_step <= 1 &&
        std::isfinite(s.chain_window_s) && s.chain_window_s >= .25f && s.chain_window_s <= 5 &&
        std::isfinite(s.fracture_threshold) && s.fracture_threshold >= 10 && s.fracture_threshold <= 10000 &&
        std::isfinite(s.bruise_threshold) && s.bruise_threshold>=0 &&
        s.bruise_threshold<=s.fracture_threshold;
}
bool is_bruised(const Injury& injury,const ScoreRules& rules) noexcept {
    return !injury.fractured && injury.severity>0 && injury.severity>=rules.bruise_threshold;
}
const char* injury_state_name(const Injury& injury,const ScoreRules& rules) noexcept {
    return injury.fractured ? "fractured" : is_bruised(injury,rules) ? "bruised" : "uninjured";
}
const char* phase_name(Phase phase) noexcept {
    switch (phase) {
    case Phase::ready: return "Ready";
    case Phase::attempt: return "Attempt";
    case Phase::bailed: return "Bailed";
    case Phase::settled: return "Settled";
    case Phase::results: return "Results";
    }
    return "Ready";
}
const char* region_name(Region region) noexcept {
    constexpr const char* names[]{"Head", "Torso", "Left arm", "Right arm", "Left leg", "Right leg"};
    const auto index = static_cast<std::size_t>(region);
    return index < region_count ? names[index] : "Unknown";
}
bool Challenge::running() const noexcept {
    return result_.phase == Phase::attempt || result_.phase == Phase::bailed || result_.phase == Phase::settled;
}
void Challenge::reset() { *this = Challenge{}; }
bool Challenge::begin(const Frame& frame, const Config& config) {
    if (!usable(frame) || frame.bailed || !valid_config(config)) return false;
    reset();
    result_.config = config;
    result_.phase = Phase::attempt;
    result_.detail = "Take a fall. Hard impacts and different body regions score more.";
    previous_ = frame;
    previous_valid_ = true;
    for (std::size_t i=0;i<frame.body_count;++i) contact_injured_[i]=frame.bodies[i].contact;
    peak_y_ = frame.center[1];
    return true;
}
void Challenge::cancel(std::string reason) {
    if (!running()) return;
    result_.cancelled = true;
    result_.phase = Phase::results;
    result_.detail = std::move(reason);
    // An interrupted attempt is never a completed score.
    result_.points = 0;
    result_.target_met = false;
    previous_valid_ = false;
}
void Challenge::finish(const char* reason) {
    update_score();
    result_.phase = Phase::results;
    result_.detail = reason;
    previous_valid_ = false;
}
void Challenge::update_score() {
    const auto& s = result_.config.scoring;
    const auto rounded = [](float value) { return static_cast<std::uint64_t>(std::llround(value)); };
    result_.fall_points = rounded(result_.fall_m*s.fall_rate);
    result_.airtime_points = rounded(result_.airtime_s*s.airtime_rate);
    result_.slide_points = rounded(result_.slide_m*s.slide_rate);
    result_.bonus_points = result_.chain_points + result_.fracture_points + result_.fall_points + result_.airtime_points + result_.slide_points;
    result_.points = result_.impact_points + result_.bonus_points;
    switch (result_.config.kind) {
    case ChallengeKind::free_play: result_.target_progress = 0; break;
    case ChallengeKind::score: result_.target_progress = static_cast<float>(result_.points); break;
    case ChallengeKind::chain: result_.target_progress = static_cast<float>(result_.best_chain); break;
    case ChallengeKind::fractures: result_.target_progress = static_cast<float>(result_.fractures); break;
    case ChallengeKind::fall: result_.target_progress = result_.fall_m; break;
    case ChallengeKind::airtime: result_.target_progress = result_.airtime_s; break;
    case ChallengeKind::slide: result_.target_progress = result_.slide_m; break;
    default: break;
    }
    result_.target_met = result_.config.kind != ChallengeKind::free_play && result_.target_progress >= result_.config.target;
}
void Challenge::step(const Frame& frame) {
    if (!running()) return;
    if (frame.entity != previous_.entity || frame.world != previous_.world) {
        cancel("Attempt cancelled: the skater or world changed.");
        return;
    }
    if (!std::isfinite(frame.dt) || frame.dt <= 0 || frame.dt > .1f || !usable(frame)) {
        // Rebaseline after any gap: never score a delta across missing samples.
        previous_valid_ = false;
        recent_impacts_={};
        // A contact chain requires observed continuity. Keep its historical
        // best, but do not carry its window or a settling timer across a gap.
        result_.current_chain = 0;
        result_.chain_remaining_s = 0;
        still_ = settled_ = 0;
        missing_time_ += std::isfinite(frame.dt) && frame.dt > 0 ? std::min(frame.dt, 1.f) : .1f;
        if (missing_time_ > .5f) cancel("Attempt cancelled: physics telemetry was lost.");
        return;
    }
    missing_time_ = 0;
    if (!previous_valid_) { airborne_ = 0; peak_y_ = frame.center[1]; }
    result_.chain_remaining_s = std::max(0.f, result_.chain_remaining_s-frame.dt);
    if (result_.chain_remaining_s <= 0) result_.current_chain = 0;
    for (std::size_t r = 0; r < region_count; ++r) {
        cooldown_[r] = std::max(0.f, cooldown_[r] - frame.dt);
        result_.injuries[r].flash = std::max(0.f, result_.injuries[r].flash - frame.dt);
    }
    for (auto& injury : result_.bone_injuries) injury.flash=std::max(0.f,injury.flash-frame.dt);
    for (std::size_t i=0;i<frame.body_count;++i) {
        auto& recent=recent_impacts_[i];
        recent.remaining=std::max(0.f,recent.remaining-frame.dt);
        if (!recent.remaining) recent={};
        if (recent.board && recent.board!=frame.board.identity) recent={};
        if (!previous_valid_ || previous_.body_count!=frame.body_count) {
            contact_injured_[i]=frame.bodies[i].contact;
            recent={};
        } else if (frame.bodies[i].joint!=previous_.bodies[i].joint ||
            frame.bodies[i].injury_joint!=previous_.bodies[i].injury_joint || frame.bodies[i].region!=previous_.bodies[i].region) {
            contact_injured_[i]=false;
            recent={};
        } else if (!frame.bodies[i].contact) contact_injured_[i]=false;
    }
    result_.elapsed_s += frame.dt;
    Vec3 displacement{};
    float speed{};
    for (std::size_t i = 0; i < frame.body_count; ++i) speed = std::max(speed, length(frame.bodies[i].velocity));
    std::array<Vec3,max_bodies> angular{};
    std::array<bool,max_bodies> angular_valid{};
    bool rotating{};
    if (previous_valid_ && previous_.bailed && frame.bailed && previous_.body_count==frame.body_count) {
        for (std::size_t i=0;i<frame.body_count;++i) {
            const auto& body=frame.bodies[i];
            const auto& before=previous_.bodies[i];
            if (body.joint!=before.joint || body.injury_joint!=before.injury_joint) continue;
            if (const auto estimate=angular_velocity(before,body,frame.dt)) {
                angular[i]=*estimate; angular_valid[i]=true;
                rotating=rotating || length(*estimate)>1.f;
            }
        }
    }
    if (previous_valid_) {
        for (std::size_t i = 0; i < 3; ++i) displacement[i] = frame.center[i] - previous_.center[i];
        if (length(displacement) > std::max(2.f, speed * frame.dt * 3.f)) {
            cancel("Attempt cancelled: teleport detected.");
            return;
        }
    }
    const bool entering_bail=result_.phase==Phase::attempt && frame.bailed;
    std::array<float,max_bodies> body_changes{};
    // Native hard-landing evidence belongs to the ridden board, not the
    // ragdoll feet. Share its arcade severity across both supporting feet.
    // A loose board, wall hit or velocity change without native cause cannot
    // transmit damage. Require continuous identity and an observed onset.
    if (previous_valid_ && previous_.body_count==frame.body_count &&
        result_.phase==Phase::attempt && !frame.manual_bail &&
        previous_.board.riding && frame.board.identity==previous_.board.identity &&
        frame.board.hard_landing && !previous_.board.hard_landing &&
        (frame.board.riding || entering_bail)) {
        const auto& board=frame.board;
        const float n=length(board.normal);
        float change{},approach{};
        for (std::size_t axis=0;axis<3;++axis) {
            const float normal=board.normal[axis]/n;
            const float incoming=previous_.board.velocity[axis]-(axis==1 ? 9.81f*frame.dt : 0);
            change+=(board.velocity[axis]-incoming)*normal;
            approach-=incoming*normal;
        }
        if (board.normal[1]/n>.5f && approach>=2 && change>=3 && board.speed>=3) {
            const float shared=std::min(change,board.speed)*.70710678f;
            for (std::size_t i=0;i<frame.body_count;++i) {
                const auto& body=frame.bodies[i];
                const auto& before=previous_.bodies[i];
                if ((body.joint!=10 && body.joint!=343) || body.joint!=before.joint ||
                    body.injury_joint!=before.injury_joint || body.region!=before.region) continue;
                if (!frame.bailed) {
                    if (shared>recent_impacts_[i].change) recent_impacts_[i]={shared,.2f,board.identity};
                } else body_changes[i]=shared;
            }
        }
    }
    if (previous_valid_ && previous_.body_count==frame.body_count &&
        (result_.phase==Phase::attempt || result_.phase==Phase::bailed)) {
        for (std::size_t i=0;i<frame.body_count;++i) {
            const auto& body=frame.bodies[i];
            const auto& before=previous_.bodies[i];
            if (!body.contact || !body.normal_valid || contact_injured_[i] || body.region!=before.region ||
                body.joint!=before.joint || body.injury_joint!=before.injury_joint) continue;
            const float n=length(body.normal);
            const auto impact_change=[&](const Vec3& incoming,const Vec3& outgoing) {
                float change{},approaching{};
                for (std::size_t axis=0;axis<3;++axis) {
                    const float normal=body.normal[axis]/n;
                    const float free_velocity=incoming[axis]-(axis==1 ? 9.81f*frame.dt : 0);
                    change+=(outgoing[axis]-free_velocity)*normal;
                    approaching-=free_velocity*normal;
                }
                return approaching>=2 && change>=3 ? change : 0.f;
            };
            float change=impact_change(before.velocity,body.velocity);
            // The native collision record preserves relative normal speed
            // even when response/animation has already slowed the body. Use
            // it only at observed contact onset, never for a held contact.
            if (!before.contact && body.speed_valid && body.contact_speed>=3)
                change=std::max(change,body.contact_speed);
            if (body.point_valid && finite(body.contact_point) && angular_valid[i] && previous_angular_valid_[i]) {
                const auto incoming=velocity_at_contact(before,previous_angular_[i],body.contact_point);
                const auto outgoing=velocity_at_contact(body,angular[i],body.contact_point);
                if (incoming && outgoing) change=std::max(change,impact_change(*incoming,*outgoing));
            }
            if (change<=0) continue;
            contact_injured_[i]=true;
            if (result_.phase==Phase::attempt && !frame.bailed) {
                if (change>recent_impacts_[i].change) recent_impacts_[i]={change,.2f};
            }
            else body_changes[i]=std::max(body_changes[i],change);
        }
    }
    if (entering_bail) {
        for (std::size_t i=0;i<frame.body_count;++i) {
            if (!frame.manual_bail && recent_impacts_[i].remaining>0)
                body_changes[i]=std::max(body_changes[i],recent_impacts_[i].change);
            recent_impacts_[i]={};
        }
    }
    if (entering_bail) {
        result_.phase = Phase::bailed;
        result_.detail = "Keep tumbling!";
    }
    if (result_.phase == Phase::attempt) {
        if (frame.grounded) { airborne_ = 0; peak_y_ = frame.center[1]; }
        else { airborne_ += frame.dt; peak_y_ = std::max(peak_y_, frame.center[1]); }
        if (result_.elapsed_s >= 60) cancel("No bail within 60 seconds. Start another attempt.");
    } else if (result_.phase == Phase::bailed) {
        bailed_time_ += frame.dt;
        if (!frame.bailed) { finish("Recovered. Your attempt is complete."); return; }
        if (!frame.grounded) { airborne_ += frame.dt; peak_y_ = std::max(peak_y_, frame.center[1]); }
        else {
            result_.airtime_s = std::max(result_.airtime_s, airborne_);
            result_.fall_m = std::max(result_.fall_m, std::max(0.f, peak_y_ - frame.center[1]));
            airborne_ = 0;
            peak_y_ = frame.center[1];
            if (previous_valid_ && previous_.grounded)
                result_.slide_m += std::sqrt(displacement[0]*displacement[0] + displacement[2]*displacement[2]);
        }
        // An arcade severity based on contact-confirmed changes of normal
        // velocity. It is not a claim of physical energy or real bone damage.
        std::array<float, region_count> severity{};
        std::array<float, injury_bone_count> bone_severity{};
        if (previous_valid_ && previous_.body_count == frame.body_count) {
            for (std::size_t i = 0; i < frame.body_count; ++i) {
                const auto& body = frame.bodies[i];
                const float change=body_changes[i];
                if (change<=0) continue;
                contact_injured_[i]=true;
                const auto region = static_cast<std::size_t>(body.region);
                const float hit=std::min(change, 60.f)*std::min(change, 60.f);
                severity[region] = std::max(severity[region], hit);
                const int injury_joint=body.injury_joint>=0 ? body.injury_joint : body.joint;
                if (injury_joint>=0) {
                    const auto bone=static_cast<std::size_t>(injury_joint);
                    bone_severity[bone]=std::max(bone_severity[bone],hit);
                }
            }
        }
        // Injury feedback follows every observed body impact, independently of
        // whether its region is eligible to award points in this sample.
        for (std::size_t bone=0;bone<bone_severity.size();++bone) {
            if (bone_severity[bone]<=0) continue;
            auto& injury=result_.bone_injuries[bone];
            injury.severity+=bone_severity[bone]; injury.flash=.65f;
            injury.fractured=injury.severity>=result_.config.scoring.fracture_threshold;
        }
        for (std::size_t r = 0; r < region_count; ++r) {
            if (severity[r] <= 0 || cooldown_[r] > 0) continue;
            cooldown_[r] = .25f;
            auto& injury = result_.injuries[r];
            injury.severity += severity[r];
            injury.flash = .65f;
            result_.largest_impact = std::max(result_.largest_impact, std::sqrt(severity[r]));
            ++result_.impacts;
            const auto& rules = result_.config.scoring;
            ++result_.current_chain;
            result_.best_chain = std::max(result_.best_chain, result_.current_chain);
            result_.chain_remaining_s = rules.chain_window_s;
            const auto base_points = static_cast<std::uint64_t>(std::llround(severity[r] * rules.impact_rate));
            result_.impact_points += base_points;
            const float multiplier = std::min(3.f, static_cast<float>(result_.current_chain-1)*rules.chain_step);
            result_.chain_points += static_cast<std::uint64_t>(std::llround(static_cast<double>(base_points)*multiplier));
            if (!injury.fractured && injury.severity >= rules.fracture_threshold) {
                injury.fractured = true;
                ++result_.fractures;
                result_.fracture_points += static_cast<std::uint64_t>(std::llround(rules.fracture_bonus));
            }
        }
        still_ = frame.grounded && speed < .65f && !rotating ? still_ + frame.dt : 0;
        if (still_ >= .8f || bailed_time_ >= 15) {
            result_.phase = Phase::settled;
            result_.detail = "Attempt complete.";
        }
    } else if (result_.phase == Phase::settled) {
        settled_ += frame.dt;
        if (settled_ >= .35f || !frame.bailed) finish("Recover, then retry for a higher score.");
    }
    if (running()) {
        update_score();
        previous_ = frame;
        previous_valid_ = true;
        previous_angular_=angular;
        previous_angular_valid_=angular_valid;
    }
}
}
