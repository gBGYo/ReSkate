#include <Windows.h>
#include <charconv>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fixture {
float value=1, replacement=.7f;
std::uintptr_t type{},field{};
unsigned writes{};
bool accept=true;
std::uintptr_t get(std::uintptr_t,const char* name,std::uintptr_t* output,bool) {
    if (std::strcmp(name,"SimulationTime.TimeScale")!=0) return 0;
    *output=type; return field;
}
bool set(std::uintptr_t,const char* name,std::uintptr_t,const void* input) {
    ++writes;
    if (!accept || std::strcmp(name,"SimulationTime.TimeScale")!=0) return false;
    *reinterpret_cast<float*>(field)=*static_cast<const float*>(input); return true;
}
}
#define DINGOSDK_NAMED_GET(base) (&fixture::get)
#define DINGOSDK_NAMED_SET(base) (&fixture::set)
// Exercise the actual settings bridge and its existing player leases with a
// typed, guarded native-call fixture. No game process or executable is used.
#include "Extension/Settings/named_settings.cpp"

namespace {
int failures{};
void check(bool condition,const char* message) {if (!condition) {++failures; std::cerr<<message<<'\n';}}
struct Image {
    void* reservation=VirtualAlloc(nullptr,dingosdk::supported_build::game_image_size,MEM_RESERVE,PAGE_READWRITE);
    std::uintptr_t base=reinterpret_cast<std::uintptr_t>(reservation);
    std::array<std::byte,512> manager{},registry{};
    Image() {
        if (!reservation) throw std::runtime_error("Cannot reserve settings fixture");
        auto& r=dingosdk::runtime();
        r=dingosdk::Runtime{}; r.base=base; r.thread=GetCurrentThreadId(); r.ready=true;
        dingosdk::NamedSettingModel model; model.name="SimulationTime.TimeScale";
        r.models.push_back(std::move(model)); r.leases.resize(1);
        put(dingosdk::contract::named_settings_manager,reinterpret_cast<std::uintptr_t>(manager.data()));
        put(dingosdk::contract::symbol_registry,reinterpret_cast<std::uintptr_t>(registry.data()));
        const std::uintptr_t bucket=base; const std::uint32_t count=1;
        std::memcpy(manager.data()+0xa0,&bucket,8); std::memcpy(manager.data()+0xa8,&count,4);
        std::memcpy(registry.data()+0xd0,&count,4);
        const auto descriptor=base+0x1000;
        put(dingosdk::contract::native_float32,descriptor);
        put(0x1004,std::uint16_t{19u<<5}); put(0x1006,std::uint16_t{4});
        fixture::type=base+dingosdk::contract::native_float32;
        fixture::field=reinterpret_cast<std::uintptr_t>(&fixture::value);
        fixture::value=1; fixture::writes=0; fixture::accept=true;
        dingosdk::set_multiplayer_session_active(false);
    }
    template<class T> void put(std::uintptr_t offset,T value) {
        const auto page=(base+offset)&~std::uintptr_t{0xfff};
        if (!VirtualAlloc(reinterpret_cast<void*>(page),4096,MEM_COMMIT,PAGE_READWRITE))
            throw std::runtime_error("Cannot commit settings fixture");
        std::memcpy(reinterpret_cast<void*>(base+offset),&value,sizeof(value));
    }
    ~Image() {dingosdk::runtime()=dingosdk::Runtime{}; VirtualFree(reservation,0,MEM_RELEASE);}
};
void player_override_and_cleanup() {
    Image image;
    auto& r=dingosdk::runtime();
    check(!dingosdk::player_change_named_setting("SimulationTime.TimeScale","0.6",false).starts_with("error"),"Player speed can be overridden before the pulse");
    check(r.leases[0] && r.leases[0]->player,"The existing player override is recorded");
    check(dingosdk::begin_impact_time_scale(.3f) && std::abs(fixture::value-.18f)<.0001f,"The bridge applies a pulse over the player's custom speed");
    (void)dingosdk::observe(0);
    check(r.leases[0] && r.leases[0]->player && std::get<float>(r.leases[0]->applied.value)==.6f,
        "A normal settings sweep cannot discard the player's underlying override during a pulse");
    check(dingosdk::update_impact_time_scale(.5f) && dingosdk::restore_impact_time_scale() && fixture::value==.6f,
        "Pulse recovery restores the custom speed through the real settings bridge");
    check(!dingosdk::player_change_named_setting("SimulationTime.TimeScale",{},true).starts_with("error") && fixture::value==1 && !r.leases[0],
        "The player's later Reset still restores the value from before their own override");
    check(dingosdk::begin_impact_time_scale(.3f),"Another pulse can begin");
    check(!dingosdk::player_change_named_setting("SimulationTime.TimeScale","0.8",false).starts_with("error") && fixture::value==.8f && !dingosdk::impact_time_scale_active(),
        "A new player command unwinds the pulse before applying the requested speed");
    check(dingosdk::restore_impact_time_scale() && fixture::value==.8f,"Later pulse cleanup preserves the new player command");
    check(dingosdk::begin_impact_time_scale(.3f),"A pulse can layer over the next custom speed");
    dingosdk::set_multiplayer_session_active(true);
    dingosdk::refresh_named_settings(false);
    check(!dingosdk::impact_time_scale_active() && fixture::value==1,"Entering multiplayer restores both the pulse and the player's locked speed override");
    check(!dingosdk::begin_impact_time_scale(.3f),"Multiplayer refuses a new impact pulse");
    dingosdk::set_multiplayer_session_active(false);
}
void external_and_rejected_writes() {
    Image image;
    check(dingosdk::begin_impact_time_scale(.3f),"A fixture pulse starts");
    fixture::value=.9f; const auto writes=fixture::writes;
    check(dingosdk::restore_impact_time_scale() && fixture::value==.9f && fixture::writes==writes,"An engine edit is preserved without a stale restoration write");
    check(dingosdk::begin_impact_time_scale(.3f),"A pulse can start after an external edit");
    fixture::field=reinterpret_cast<std::uintptr_t>(&fixture::replacement);
    const auto before_reuse=fixture::writes;
    check(dingosdk::restore_impact_time_scale() && fixture::replacement==.7f && fixture::writes==before_reuse,"Native field replacement cannot receive the old object's speed");
    fixture::accept=false;
    check(!dingosdk::begin_impact_time_scale(.3f) && fixture::replacement==.7f && !dingosdk::impact_time_scale_active(),"A rejected native setter leaves no active pulse");
}
void cleanup_thread_ownership() {
    Image image;
    check(dingosdk::begin_impact_time_scale(.3f),"A pulse can start before a loading callback");
    const auto writes=fixture::writes;
    bool restored=true;
    std::thread callback([&] {restored=dingosdk::restore_impact_time_scale();});
    callback.join();
    check(!restored && fixture::writes==writes && dingosdk::impact_time_scale_active(),
        "A loading callback on another thread cannot write native settings or lose pending cleanup");
    check(dingosdk::restore_impact_time_scale() && fixture::value==1,
        "The next owning-thread update restores speed after an off-thread loading callback");
}
}
int main() {
    player_override_and_cleanup(); external_and_rejected_writes(); cleanup_thread_ownership();
    if (failures) return 1;
    std::cout<<"Slam time-scale settings ownership checks passed.\n";
}
