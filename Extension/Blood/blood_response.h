#pragma once
#include <algorithm>
#include <cmath>

namespace dingosdk::blood {
enum class BloodIntensity : unsigned { light, medium, heavy };
inline BloodIntensity blood_intensity(float damage) noexcept {
    if (!std::isfinite(damage) || damage<100) return BloodIntensity::light;
    return damage<324 ? BloodIntensity::medium : BloodIntensity::heavy;
}
inline constexpr float blood_max_emission_scale=2.f;
struct BloodImpactResponse {
    float spray_scale, drop_scale, emission, stain_scale, trail_scale, bleed_seconds;
};
// Damage is squared contact speed, not fall height or accumulated score.
// Reach the ordinary look at 12 m/s and full intensity at 25 m/s; heavy
// particle bursts start at 18 m/s. Tiny falls retain a visible 5 m/s floor.
inline BloodImpactResponse blood_impact_response(float damage) noexcept {
    const float speed=std::sqrt(std::isfinite(damage) ? std::clamp(damage,0.f,3600.f) : 0.f);
    const float response=speed<12 ? std::clamp((speed-12)/7,-1.f,0.f) : std::clamp((speed-12)/13,0.f,1.f);
    const float heavy=std::max(0.f,response);
    return {1+response*(response<0 ? .2f : 1.28f),
        1+.35f*heavy, // Never shrink the already tiny attached droplets.
        1+response*(response<0 ? .65f : blood_max_emission_scale-1),
        1+response*(response<0 ? .35f : 2.456f),
        1+response*(response<0 ? .2f : 1.f),
        8+response*(response<0 ? 5.f : 4.f)};
}
}
