#include "Extension/Blood/blood_runtime.h"
#include "Extension/Blood/blood_native.h"
#include "Extension/Skater/no_bail.h"
#include "Extension/Profile/local_profile_runtime.h"
#include "Engine/Game/Build/20260929/blood_contacts.h"
#include "Engine/Game/Build/20260929/client_source_spawn.h"
#include "Engine/Game/Build/20260929/replay_activity.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using namespace dingosdk;
namespace b=dingosdk::blood;
namespace layout=game::build::v20260929::blood_contacts;
LocalBailOwner local;
bool owner_available=true, particles_allowed{}, ground_allowed{};
std::uint64_t rendered_entity{}, accepted{};
std::size_t sources{}, marks{};
unsigned direct_resolves{};
int failures{};
void check(bool value,const char* message) {
    if (!value) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
template<class T> void put(std::uintptr_t address,const T& value) {
    std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));
}
struct Fixture {
    // Fake game image and objects, read through the real guarded memory reader.
    // Native ownership and rendering are replaced below; no engine is invoked.
    std::vector<unsigned char> memory=std::vector<unsigned char>(0x7800000);
    std::uintptr_t base=reinterpret_cast<std::uintptr_t>(memory.data());
    std::uintptr_t client=base+0x10000, physics=base+0x30000, parts=base+0x32000;
    std::uintptr_t reporter=base+0x35000, pose=base+0x38000, matrices=base+0x40000;
    const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    Fixture() {
        local={base,base+0x20000,base+0x21000,base+0x23000,base+0x24000,base+0x26000,base+0x2c000};
        for (const auto& c:{layout::contacts,layout::contacts_before_skeleton,layout::contact_record_reset,
            layout::contact_peak,layout::contact_kinds,layout::skeleton,layout::body_transform}) put(base+c.rva,c.bytes);
        bind_entity(local.entity);
        put(local.rig+layout::pose_output,pose);
        put(pose,matrices); put(pose+8,matrices); put(pose+0x1c,std::uint32_t{395});
        put(local.rig+layout::physics,physics);
        put(physics,base+game::build::v20260929::client_source_spawn::rig_physics_vtable);
        put(physics+0x20,parts); put(physics+layout::reporter,reporter); put(parts,std::uint32_t{26});
        constexpr std::array joints{380,102,101,278,277,276,275,49,48,47,46,45,44,43,42,344,343,342,341,11,10,9,8,7};
        for (unsigned i=0;i<joints.size();++i) {
            put(local.rig+layout::body_map+4*i,joints[i]); put(local.rig+layout::body_parents+4*i,int{-1});
            put(matrices+0x40*joints[i],identity);
            put(parts+i*layout::body_stride+0x10,physics);
            put(parts+i*layout::body_stride+layout::body_pose,identity);
        }
        put(local.context+0x1414,std::uint32_t{200});
    }
    void bind_entity(std::uintptr_t entity) {
        const auto collection=base+0x22000;
        put(entity+0x70,collection); put(collection,entity); put(collection+0x10,identity);
    }
    void tick() {b::tick(base,client,true,false,false,"multiplayer-map");}
    void step(bool bail=false) {
        const auto contacts=b::capture_contacts(local.rig);
        check(contacts.valid,"The watched local rig supplies contacts");
        b::observe_skeleton(local.rig,.02f,bail,contacts);
    }
    void impact() {
        put(reporter+layout::contact_flags+1,std::uint8_t{0});
        put(local.context+0x1414,std::uint32_t{200});
        put(parts+layout::body_stride+layout::velocity,b::Vec3{0,-12,0});
        step(); step();
        const auto record=reporter+layout::normals+layout::normal_stride;
        put(reporter+layout::contact_flags+1,std::uint8_t{1});
        put(record,b::Vec3{0,1,0}); put(record+0x50,12.f);
        put(local.context+0x1414,std::uint32_t{300});
        b::observe_selection(local.selector,300); step(true); tick();
    }
};
}
namespace dingosdk {
bool no_bail_available() noexcept {return true;}
bool resolve_local_bail_owner(std::uintptr_t,LocalBailOwner& out) noexcept {
    ++direct_resolves; out=local; return owner_available;
}
bool resolve_local_bail_owner(std::uintptr_t,std::uintptr_t entity,LocalBailOwner& out) noexcept {
    out=local; return owner_available && entity==local.entity;
}
bool local_bail_recovered(const LocalBailOwner&) noexcept {return false;}
namespace profile_runtime {
std::optional<Json> local_value(std::string_view) noexcept {return {};}
void set_local_values(const std::vector<std::pair<std::string,Json>>&) noexcept {}
}
namespace blood {
BloodStatus update_native_blood(std::uintptr_t,const BloodScene& scene,bool particles,bool ground) noexcept {
    particles_allowed=particles; ground_allowed=ground; rendered_entity=scene.entity; accepted=scene.accepted;
    sources=particles ? std::count_if(scene.sources.begin(),scene.sources.end(),[](const auto& x){return x.id!=0;}) : 0;
    marks=ground ? std::count_if(scene.ground.marks.begin(),scene.ground.marks.end(),[](const auto& x){return x.id!=0;}) : 0;
    return {true,sources,"Native test renderer ready.",marks};
}
void stop_native_blood() noexcept {particles_allowed=ground_allowed=false; sources=marks=0;}
}
}
int main() {
    Fixture f;
    f.tick(); b::Options options; options.blood=true;
    check(b::set_options(options),"Blood can be enabled for a resolved local player");
    f.tick();
    check(direct_resolves>=2,"Blood resolves the player without a debug UI identity");
    check(!b::capture_contacts(local.rig+0x100).valid,"Remote rigs cannot supply blood contacts");
    auto remote=b::PhysicsContacts{}; remote.valid=true;
    b::observe_selection(local.selector+0x100,300);
    b::observe_skeleton(local.rig+0x100,.02f,true,remote); f.tick();
    check(sources==0 && marks==0,"Remote selection and physics events cannot produce blood");
    f.impact();
    check(accepted>0 && sources>0 && marks>0 && rendered_entity==local.entity,
        "Local impact produces particles and marks while remote events are present");
    const auto previous_accepted=accepted;
    b::observe_skeleton(local.rig+0x100,.02f,true,remote); f.tick();
    check(accepted==previous_accepted,"Remote impacts do not add wounds to the local scene");
    b::tick(f.base,f.client,true,true,false,"multiplayer-map");
    check(!particles_allowed && !ground_allowed,"Noclip clears both effect paths");
    f.tick(); f.impact();
    check(sources>0,"Local blood resumes after Noclip ends");
    owner_available=false; f.tick();
    check(sources==0 && marks==0,"Lost local ownership clears blood");
    owner_available=true; local.entity=f.base+0x50000; f.bind_entity(local.entity); f.tick(); f.impact();
    check(sources>0 && rendered_entity==local.entity,"Respawn binds directly to the replacement local player");
    put(f.base+game::build::v20260929::replay_activity::playing,std::uint8_t{1}); f.tick();
    check(!particles_allowed && !ground_allowed,"Replay playback suspends blood");
    put(f.base+game::build::v20260929::replay_activity::playing,std::uint8_t{0});
    b::before_level_transition(22); f.tick();
    check(!particles_allowed && !ground_allowed,"Level teardown invalidates blood");
    b::before_level_transition(21); f.tick(); f.impact();
    check(sources>0,"Blood resumes in the new world");
    put(local.rig+layout::pose_output,std::uintptr_t{}); f.step(); f.tick();
    check(b::snapshot().status.find("physics and skeleton")!=std::string::npos,
        "Rejected samples report their cause instead of claiming readiness");
    return failures ? 1 : 0;
}
