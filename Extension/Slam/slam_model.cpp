#include "slam_model.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace dingosdk::slam {
namespace {
float length(const Vec3& v) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }
bool finite(const Vec3& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x) && std::abs(x) < 1000000; });
}
bool usable(const Frame& frame) {
    if (!frame.valid || !frame.entity || !frame.world || !finite(frame.center) ||
        !frame.body_count || frame.body_count > max_bodies) return false;
    for (std::size_t i = 0; i < frame.body_count; ++i) {
        const auto& body = frame.bodies[i];
        if (body.region >= Region::count || body.joint< -1 || body.joint>=static_cast<int>(injury_bone_count) ||
            !finite(body.velocity) || length(body.velocity) > 300 ||
            (body.contact && (!finite(body.normal) || length(body.normal) < .5f || length(body.normal) > 1.5f)))
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
        std::isfinite(s.head_fracture) && s.head_fracture >= 10 && s.head_fracture <= 10000 &&
        std::isfinite(s.limb_fracture) && s.limb_fracture >= 10 && s.limb_fracture <= 10000;
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
    result_.elapsed_s += frame.dt;
    Vec3 displacement{};
    float speed{};
    for (std::size_t i = 0; i < frame.body_count; ++i) speed = std::max(speed, length(frame.bodies[i].velocity));
    if (previous_valid_) {
        for (std::size_t i = 0; i < 3; ++i) displacement[i] = frame.center[i] - previous_.center[i];
        if (length(displacement) > std::max(2.f, speed * frame.dt * 3.f)) {
            cancel("Attempt cancelled: teleport detected.");
            return;
        }
    }
    if (result_.phase == Phase::attempt && frame.bailed) {
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
                const auto& before = previous_.bodies[i];
                if (!body.contact || body.region != before.region) continue;
                const float n = length(body.normal);
                float change{}, approaching{};
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    const float normal = body.normal[axis] / n;
                    // Remove gravity so a free fall cannot look like an impact.
                    const float free_velocity = before.velocity[axis] - (axis == 1 ? 9.81f * frame.dt : 0);
                    change += (body.velocity[axis] - free_velocity) * normal;
                    approaching -= free_velocity * normal;
                }
                if (approaching < 2 || change < 3) continue;
                const auto region = static_cast<std::size_t>(body.region);
                const float hit=std::min(change, 60.f)*std::min(change, 60.f);
                severity[region] = std::max(severity[region], hit);
                if (body.joint>=0) {
                    const auto bone=static_cast<std::size_t>(body.joint);
                    bone_severity[bone]=std::max(bone_severity[bone],hit);
                }
            }
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
            const float threshold = r == static_cast<std::size_t>(Region::head) ? rules.head_fracture : rules.limb_fracture;
            for (std::size_t i=0;i<frame.body_count;++i) {
                const auto& body=frame.bodies[i];
                if (body.region!=static_cast<Region>(r) || body.joint<0) continue;
                const auto bone=static_cast<std::size_t>(body.joint);
                if (bone_severity[bone]<=0) continue;
                auto& bone_injury=result_.bone_injuries[bone];
                bone_injury.severity+=bone_severity[bone]; bone_injury.flash=.65f;
                bone_injury.fractured=bone_injury.severity>=threshold;
                bone_severity[bone]=0; // Repeated body mappings cannot double-count a bone.
            }
            if (!injury.fractured && injury.severity >= threshold) {
                injury.fractured = true;
                ++result_.fractures;
                result_.fracture_points += static_cast<std::uint64_t>(std::llround(rules.fracture_bonus));
            }
        }
        still_ = frame.grounded && speed < .65f ? still_ + frame.dt : 0;
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
    }
}
}
