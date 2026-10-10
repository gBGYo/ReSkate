#pragma once
#include "impact_model.h"
#include "blood_ground.h"
#include "blood_response.h"
#include <span>

namespace dingosdk::blood {
enum class BloodKind : unsigned { spray, droplets, trail };
inline constexpr std::size_t max_blood_sources=24;
struct BloodOptions { bool enabled{}, reduced{}; float strength=1, minimum_damage=25; BloodTuning tuning; };
struct BloodSource {
    std::uint64_t id{};
    BloodKind kind{};
    BloodIntensity intensity=BloodIntensity::medium;
    std::array<float,16> transform{};
    Vec3 local_point{};
    Vec3 local_direction{};
    int joint=-1;
    float age{}, emit_seconds{}, lifetime{};
    bool emitting{};
};
// Physics owns this model. Client tick consumes an immutable scene and owns
// native handles. Ages use simulation time, including the game's slow motion.
struct BloodScene {
    std::uint64_t entity{},world{},generation{};
    std::array<BloodSource,max_blood_sources> sources{};
    BloodColor color=BloodColor::red;
    BloodGroundScene ground;
    std::uint64_t accepted{},dropped{};
};
class BloodModel {
public:
    void step(const Frame& frame,std::span<const ImpactContact> impacts,
        const BloodOptions& options,std::uint64_t generation) noexcept;
    void clear() noexcept;
    void suspend() noexcept;
    const BloodScene& scene() const noexcept {return scene_;}
private:
    BloodScene scene_;
    BloodGroundModel ground_;
    std::array<std::uint64_t,max_bodies> episodes_{};
    std::uint64_t next_id_{};
    bool bailed_{};
};
}
