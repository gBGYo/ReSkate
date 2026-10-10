#pragma once
#include "impact_model.h"
#include "blood_settings.h"

namespace dingosdk::blood {
inline constexpr std::size_t max_blood_marks=2048;
inline constexpr float blood_mark_lifetime=90.f;
inline constexpr float blood_mark_fade_seconds=5.f, blood_pressure_fade_seconds=2.f;
// Leave room for new impacts while retired marks finish fading.
inline constexpr std::size_t blood_mark_soft_limit=max_blood_marks-256;
// Trails cannot consume these slots, even while every existing mark is fresh.
inline constexpr std::size_t blood_impact_reserve=128;
enum class BloodMarkKind : std::uint8_t {smear,broken,clotted,drop,body};
inline constexpr std::array blood_ground_materials={
    "world/materials/decal/reskate_blood/reskate_blood_dv",
    "world/materials/decal/reskate_blood/reskate_blood_broken_dv",
    "world/materials/decal/reskate_blood/reskate_blood_clotted_dv",
    "world/materials/decal/reskate_blood/reskate_blood_drop_dv"};
struct BloodMark {
    std::uint64_t id{};
    std::array<float,16> transform{};
    float age{};
    float lifetime=blood_mark_lifetime;
    float retirement=-1;
    BloodMarkKind kind{};
    std::uint8_t body_variant{};
    bool impact{};
    bool vehicle{};
    float projection_lift=.04f;
    float opacity() const noexcept;
};
static_assert(sizeof(BloodMark)==96);
struct BloodGroundStats {
    std::uint64_t spawned{}, density_rejected{}, pressure_rejected{}, retirement_started{}, retired{}, expired{};
    std::uint64_t neighbour_checks{};
};
struct BloodGroundScene {
    std::uint64_t entity{},world{},generation{};
    std::array<BloodMark,max_blood_marks> marks{};
    BloodGroundStats stats;
    BloodColor color=BloodColor::red;
};
// Simulation owns contact sampling and mark lifetime. Marks survive recovery;
// a new world, disabled effects or a changed skater clears the scene.
class BloodGroundModel {
public:
    void step(const Frame&,std::span<const ImpactContact>,bool enabled,bool reduced,float strength,std::uint64_t generation,float minimum_damage=25,const BloodTuning& tuning={}) noexcept;
    void clear() noexcept;
    void suspend() noexcept {wounds_={}; episodes_={};}
    const BloodGroundScene& scene() const noexcept {return scene_;}
private:
    struct Wound {
        int joint=-1;
        float remaining{},distance{},spacing{},severity{};
        std::uint32_t random{};
        unsigned body_variant=3;
        Vec3 point{},normal{},body_position{};
        bool tracking{},torso{},pending_impact{},vehicle{};
    };
    BloodGroundScene scene_;
    BloodTuning tuning_;
    std::array<Wound,max_bodies> wounds_{};
    std::array<std::uint64_t,max_bodies> episodes_{};
    std::uint64_t next_id_{};
    std::size_t next_slot_{};
    std::size_t active_{},retiring_{};
    Vec3 focus_{};
    // Three-dimensional metre grid, including tall walls and stacked props.
    // Links are slot+1; zero is empty.
    struct CellEntry {int x{},y{},z{}; std::uint16_t next{},previous{};};
    std::array<std::uint16_t,4096> cells_{};
    std::array<CellEntry,max_blood_marks> cell_entries_{};
    float largest_radius_{};
    void index(std::size_t slot) noexcept;
    void unindex(std::size_t slot) noexcept;
    bool crowded(const BloodMark& candidate) noexcept;
    void retire() noexcept;
    void stamp(Vec3 point,Vec3 normal,Vec3 direction,float width,float length,BloodMarkKind kind,unsigned body_variant=0,bool impact=false,bool vehicle=false) noexcept;
    void deposit(Wound&,Vec3 point,Vec3 normal,Vec3 direction,float width,bool initial,bool reduced,unsigned& budget) noexcept;
};
using BloodMatrix=std::array<float,16>;
using BloodReceiverId=std::array<std::uint64_t,2>; // Physics world, packed body index/generation.
struct BloodReceiver {BloodReceiverId id{}; BloodMatrix pose{};};
struct BloodGroundFunctions {
    void* context{};
    std::uint32_t (*create)(void*,const BloodMark&){};
    void (*release)(void*,std::uint32_t&){};
    void (*opacity)(void*,std::uint32_t,float){};
    bool (*bind)(void*,const BloodMark&,BloodReceiver&){};
    bool (*pose)(void*,const BloodReceiverId&,BloodMatrix&){};
    void (*move)(void*,std::uint32_t,const BloodMatrix&){};
};
class BloodGroundPool {
public:
    void update(const BloodGroundScene&,const BloodGroundFunctions&,bool allowed);
    void clear(const BloodGroundFunctions&);
    std::size_t active() const noexcept;
    std::size_t failed() const noexcept;
    std::size_t attached() const noexcept;
private:
    struct Entry {
        std::uint64_t id{}; std::uint32_t handle{}; std::uint8_t opacity=255; bool failed{};
        BloodReceiverId receiver{};
        BloodMatrix local{},drawn{};
    };
    std::array<Entry,max_blood_marks> entries_{};
    std::uint64_t entity_{},world_{},generation_{};
};
}
