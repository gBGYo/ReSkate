#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dingosdk::blood {
using Vec3 = std::array<float, 3>;
enum class Region : std::uint8_t { head, torso, left_arm, right_arm, left_leg, right_leg, count };
inline constexpr std::size_t max_bodies=26, injury_bone_count=395;

// One simulation sample. Contacts must be confirmed by the native contact
// report; velocity changes on their own (gravity/animation) are never impacts.
struct ContactKinds {
    bool board{}, vehicle{}, world{}, kind_5{}, kind_11{};
    bool board_only() const noexcept { return board && !vehicle && !world && !kind_5 && !kind_11; }
};
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
    ContactKinds hit;
};
struct Frame {
    std::uint64_t entity{}, world{};
    float dt{};
    Vec3 center{};
    std::array<Body, max_bodies> bodies{};
    std::size_t body_count{};
    bool valid{}, bailed{}, grounded{};
};
// Accepted physical body contacts. Geometry
// belongs to the sample that produced the hit, including a hit just before bail.
// Board-supported landing damage deliberately does not produce these events.
struct ImpactContact {
    Body body;
    Vec3 incoming_velocity{};
    std::uint64_t episode{};
    std::size_t body_index{};
    float speed{}, severity{};
    bool new_episode{};
};
// Read-only contact episodes extracted from the Hall of Meat detector. No
// scores, injuries, challenge lifecycle or presentation effects are produced.
class ImpactTracker {
public:
    bool begin(const Frame&);
    void step(const Frame&);
    void reset() noexcept;
    bool running() const noexcept {return active_;}
    std::span<const ImpactContact> impact_contacts() const noexcept {return {impacts_.data(),impact_count_};}
private:
    std::array<ImpactContact,max_bodies> impacts_{};
    std::size_t impact_count_{};
    std::uint64_t next_episode_{};
    Frame previous_;
    struct ContactEpisode {
        float speed{},severity{},remaining{};
        bool blocked{},native{};
        std::uint64_t serial{};
    };
    std::array<ContactEpisode,max_bodies> contacts_{};
    struct RecentImpact {float change{},remaining{}; ImpactContact physical;};
    std::array<RecentImpact,max_bodies> recent_impacts_{};
    std::array<Vec3,max_bodies> previous_angular_{};
    std::array<bool,max_bodies> previous_angular_valid_{};
    bool active_{},bailed_{},previous_valid_{};
};
}
