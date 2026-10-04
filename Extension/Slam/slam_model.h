#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dingosdk::slam {
using Vec3 = std::array<float, 3>;
enum class Phase { ready, attempt, bailed, settled, results };
enum class Region : std::uint8_t { head, torso, left_arm, right_arm, left_leg, right_leg, count };
enum class ChallengeKind : std::uint8_t { free_play, score, chain, fractures, fall, airtime, slide, count };
const char* challenge_name(ChallengeKind kind) noexcept;
const char* challenge_unit(ChallengeKind kind) noexcept;
float default_target(ChallengeKind kind) noexcept;
struct ScoreRules {
    float impact_rate = 10, fracture_bonus = 500;
    float fall_rate = 100, airtime_rate = 50, slide_rate = 25;
    float chain_step = .25f, chain_window_s = 1.5f;
    float head_fracture = 160, limb_fracture = 225;
    bool operator==(const ScoreRules&) const = default;
};
struct Config {
    ChallengeKind kind = ChallengeKind::free_play;
    float target = 5000;
    ScoreRules scoring;
    bool operator==(const Config&) const = default;
};
bool valid_config(const Config& config) noexcept;
inline constexpr std::size_t region_count = static_cast<std::size_t>(Region::count);
inline constexpr std::size_t max_bodies = 26;
inline constexpr std::size_t injury_bone_count = 395;
const char* phase_name(Phase phase) noexcept;
const char* region_name(Region region) noexcept;

// One simulation sample. Contacts must be confirmed by the native contact
// report; velocity changes on their own (gravity/animation) are never impacts.
struct Body {
    Region region = Region::torso;
    Vec3 velocity{}, normal{};
    bool contact{};
    int joint = -1;
    // Physical joint identity and displayed anatomy are distinct. The native
    // head body is attached to Neck1 (102), but injures the skull (103).
    int injury_joint = -1;
    bool normal_valid = true;
    // Optional world-space rigid-body pose and a verified contact point.
    // Rotation is derived from consecutive poses; no native angular field is
    // guessed. Missing/ambiguous point detail keeps the linear estimate.
    std::array<float,16> pose{};
    Vec3 contact_point{};
    bool pose_valid{}, point_valid{};
    // Contact producer's relative normal speed, captured before animation
    // correction can erase the body's incoming linear velocity.
    float contact_speed{};
    bool speed_valid{};
};
// Owned board sample. A hard landing requires native cause 6, an upward
// support normal and a matching normal velocity loss; speed alone is insufficient.
struct BoardLanding {
    std::uint64_t identity{};
    Vec3 velocity{}, normal{};
    float speed{};
    bool riding{}, hard_landing{};
};
struct Frame {
    std::uint64_t entity{}, world{};
    float dt{};
    Vec3 center{};
    std::array<Body, max_bodies> bodies{};
    std::size_t body_count{};
    bool valid{}, bailed{}, grounded{}, manual_bail{};
    BoardLanding board;
};
struct Injury {
    float severity{}, flash{};
    bool fractured{};
};
struct Result {
    Config config;
    Phase phase = Phase::ready;
    std::uint64_t points{}, impact_points{}, bonus_points{};
    std::uint64_t chain_points{}, fracture_points{}, fall_points{}, airtime_points{}, slide_points{};
    unsigned impacts{}, fractures{};
    unsigned current_chain{}, best_chain{};
    float chain_remaining_s{}, target_progress{};
    float fall_m{}, airtime_s{}, slide_m{}, elapsed_s{}, largest_impact{};
    std::array<Injury, region_count> injuries{};
    std::array<Injury, injury_bone_count> bone_injuries{};
    bool cancelled{}, target_met{};
    std::string detail = "Start an attempt, then take a fall.";
};

// Pure scoring/lifecycle. Native pointers and rendering never enter this model.
class Challenge {
public:
    bool begin(const Frame& frame, const Config& config = {});
    void step(const Frame& frame);
    void cancel(std::string reason);
    void reset();
    const Result& result() const noexcept { return result_; }
    bool running() const noexcept;
private:
    void finish(const char* reason);
    void update_score();
    Result result_;
    Frame previous_;
    std::array<float, region_count> cooldown_{};
    // A held contact can injure each body once. Scoring cooldowns are regional;
    // they must never suppress a different body's injury in the same region.
    std::array<bool, max_bodies> contact_injured_{};
    struct RecentImpact {float change{}, remaining{}; std::uint64_t board{};};
    std::array<RecentImpact,max_bodies> recent_impacts_{};
    std::array<Vec3, max_bodies> previous_angular_{};
    std::array<bool, max_bodies> previous_angular_valid_{};
    float airborne_{}, peak_y_{}, still_{}, settled_{}, bailed_time_{}, missing_time_{};
    bool previous_valid_{};
};
}
