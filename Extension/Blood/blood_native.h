#pragma once
#include "blood.h"
#include <string_view>

namespace dingosdk::blood {
// Keep the original medium assets first so their private identities stay stable.
inline constexpr std::array<const char*,9> blood_asset_names{
    "effects/reskate/hallofmeat/ebp_blood_spray",
    "effects/reskate/hallofmeat/ebp_blood_droplets",
    "effects/reskate/hallofmeat/ebp_blood_trail",
    "effects/reskate/hallofmeat/ebp_blood_spray_light",
    "effects/reskate/hallofmeat/ebp_blood_droplets_light",
    "effects/reskate/hallofmeat/ebp_blood_trail_light",
    "effects/reskate/hallofmeat/ebp_blood_spray_heavy",
    "effects/reskate/hallofmeat/ebp_blood_droplets_heavy",
    "effects/reskate/hallofmeat/ebp_blood_trail_heavy"};
inline constexpr unsigned blood_asset_index(BloodKind kind,BloodIntensity intensity) noexcept {
    return unsigned(kind)+(intensity==BloodIntensity::light ? 3 : intensity==BloodIntensity::heavy ? 6 : 0);
}
struct BloodNativeFunctions {
    void* context{};
    std::uintptr_t (*create)(void*,BloodKind,BloodIntensity,const std::array<float,16>&){};
    void (*move)(void*,std::uintptr_t&,const std::array<float,16>&){};
    void (*stop)(void*,std::uintptr_t&,bool){};
    void (*release)(void*,std::uintptr_t&){};
};
// Owns exactly one native reference per source. Testable without engine state.
class BloodNativePool {
public:
    void update(const BloodScene& scene,const BloodNativeFunctions& functions,bool allowed);
    void clear(const BloodNativeFunctions& functions);
    std::size_t active() const noexcept;
private:
    struct Entry {std::uint64_t id{}; std::uintptr_t handle{}; bool stopped{};};
    std::array<Entry,max_blood_sources> entries_{};
    std::uint64_t entity_{},world_{},generation_{};
};
struct BloodStatus {bool available{}; std::size_t active{}; std::string_view detail; std::size_t marks{};};
BloodStatus update_native_blood(std::uintptr_t base,const BloodScene& scene,bool allowed,bool ground_allowed) noexcept;
BloodStatus update_native_blood_ground(std::uintptr_t base,const BloodGroundScene& scene,bool allowed) noexcept;
void stop_native_blood_ground() noexcept;
// Called before loading destroys the native scene; never from Present/physics.
void stop_native_blood() noexcept;
}
