#include "blood_native.h"
#include "blood_draw_budget.h"
#include "blood_receiver.h"
#include "Extension/Decals/native_decals.h"
#include "Engine/Game/Abi/native_data.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Core/Log/logging.h"
#include <Windows.h>
#include <mutex>
#include <algorithm>
#include <cmath>

namespace dingosdk::blood {
namespace {
namespace decal=decals::native;
struct State {
    std::mutex mutex;
    BloodGroundPool pool;
    NativeBloodReceivers receivers;
    BloodDrawBudget draw_budget;
    std::uint64_t budget_at{};
    bool budget_wanted{},budget_ready=true;
    std::uintptr_t base{},manager{};
    std::array<std::uintptr_t,blood_ground_materials.size()> materials{};
    std::uint64_t world{},generation{},lookup_at{};
    std::uint64_t diagnostic_at{};
    BloodColor color=BloodColor::red;
    DWORD thread{};
    bool checked{},valid{},stop_requested{},diagnostic_active{};
};
State& state() {static auto* s=new State; return *s;}
void budget(State& s,bool wanted) {
    const auto now=GetTickCount64();
    if (wanted==s.budget_wanted && now<s.budget_at) return;
    const bool ready=s.draw_budget.update(s.base,wanted ? std::uint32_t(max_blood_marks) : 0);
    if (wanted!=s.budget_wanted || ready!=s.budget_ready)
        logging::log(ready ? logging::Level::info : logging::Level::warning,logging::Channel::skater,
            "Blood surface: decal draw allowance {}, ready {}.",wanted ? max_blood_marks : 0,ready);
    s.budget_wanted=wanted; s.budget_ready=ready; s.budget_at=now+1000;
}
std::uint32_t create(void* context,const BloodMark& mark) {
    auto& s=*static_cast<State*>(context);
    const auto& transform=mark.transform;
    // Reuse the three visible limb materials for varied torso silhouettes.
    // A separate torso material previously disappeared with camera angle.
    if (mark.kind==BloodMarkKind::body && mark.body_variant>=3) return 0;
    const auto kind=mark.kind==BloodMarkKind::body ? mark.body_variant : static_cast<unsigned>(mark.kind);
    if (kind>=s.materials.size() || !s.materials[kind]) return 0;
    const auto handle=decal::create(s.base,s.manager,s.materials[kind],transform,decal::ReceiverMask::all);
    logging::log(logging::Level::debug,logging::Channel::skater,
        "Blood surface: created {}, kind {}, impact {}, size ({:.3f}, {:.3f}), at ({:.3f}, {:.3f}, {:.3f}).",
        handle.id,kind,mark.impact,
        std::sqrt(transform[0]*transform[0]+transform[1]*transform[1]+transform[2]*transform[2]),
        std::sqrt(transform[8]*transform[8]+transform[9]*transform[9]+transform[10]*transform[10]),
        transform[12],transform[13],transform[14]);
    return handle.id;
}
void release(void* context,std::uint32_t& id) {
    const auto& s=*static_cast<State*>(context);
    decal::Handle handle{id}; decal::release(s.base,handle);
    logging::log(logging::Level::debug,logging::Channel::skater,"Blood surface: released {}.",id);
    id=0;
}
void opacity(void* context,std::uint32_t id,float value) {
    const auto& s=*static_cast<State*>(context);
    decal::set_opacity(s.base,s.manager,id,value);
}
BloodGroundFunctions functions(State& s) {
    return {&s,create,release,opacity,
        [](void* c,const BloodMark& mark,BloodReceiver& out){
            return static_cast<State*>(c)->receivers.bind(mark,out);
        },
        [](void* c,const BloodReceiverId& id,BloodMatrix& out){return static_cast<State*>(c)->receivers.pose(id,out);},
        [](void* c,std::uint32_t id,const BloodMatrix& transform){
            const auto& s=*static_cast<State*>(c); decal::set_transform(s.base,s.manager,id,transform);
        }};
}
std::uintptr_t find_material(std::uintptr_t base,const char* name) {
    const auto find=game::native_data().find_asset;
    if (!find) return 0;
    for (std::uint16_t domain=0;domain<0xbbf;++domain) {
        std::uintptr_t owner{};
        if (memory::read(base+game::build::v20260929::engine::domain_owners+8ULL*domain,owner) && owner)
            if (auto asset=find(domain,name)) return asset;
    }
    return 0;
}
}
BloodStatus update_native_blood_ground(std::uintptr_t base,const BloodGroundScene& scene,bool allowed) noexcept {
    try {
        auto& s=state(); std::lock_guard lock(s.mutex);
        if (!s.thread) s.thread=GetCurrentThreadId();
        if (s.thread!=GetCurrentThreadId()) return {false,s.pool.active(),"Ground trails are waiting for the game thread."};
        if (!s.checked) {
            s.base=base;
            s.valid=base && decal::contracts_match(base) && decal::opacity_contracts_match(base) &&
                decal::surface_contracts_match(base) && s.receivers.initialize(base);
            s.checked=true;
        }
        if (!s.valid || base!=s.base) return {false,0,"Ground trails are unavailable for this game build."};
        const auto manager=decal::manager(base);
        if (s.manager && s.manager!=manager) {s.pool={}; s.materials={}; s.world=s.generation=0;}
        s.manager=manager;
        if (!manager) {budget(s,false); return {false,0,"Ground trails are waiting for the world."};}
        const auto f=functions(s);
        if (s.stop_requested || !allowed || s.world!=scene.world || s.generation!=scene.generation || s.color!=scene.color) {
            if (s.pool.active()) logging::log(logging::Level::debug,logging::Channel::skater,
                "Blood surface: clearing {} owned marks (stop {}, allowed {}, world changed {}, generation changed {}).",
                s.pool.active(),s.stop_requested,allowed,s.world!=scene.world,s.generation!=scene.generation);
            s.pool.clear(f); budget(s,false); s.materials={}; s.lookup_at=0; s.stop_requested=false;
        }
        s.world=scene.world; s.generation=scene.generation; s.color=scene.color;
        if (!allowed || !scene.entity || !scene.world) {s.pool.clear(f); budget(s,false); return {true,0,"Ground trails are ready."};}
        if (GetTickCount64()>=s.lookup_at) {
            s.lookup_at=GetTickCount64()+2000;
            for (unsigned i=0;i<s.materials.size();++i)
                if (!s.materials[i]) s.materials[i]=find_material(base,blood_colored_asset(blood_ground_materials[i],scene.color).c_str());
        }
        if (std::any_of(s.materials.begin(),s.materials.end(),[](auto material){return !material;})) return {false,0,"Selected ground blood color is not installed. Update the blood mod."};
        budget(s,std::any_of(scene.marks.begin(),scene.marks.end(),[](const auto& mark){return mark.id!=0;}));
        s.receivers.begin(scene.world);
        s.pool.update(scene,f,true);
        const auto owned=s.pool.active(),failed=s.pool.failed();
        if ((owned || failed) ? GetTickCount64()>=s.diagnostic_at : s.diagnostic_active) {
            s.diagnostic_active=owned || failed;
            s.diagnostic_at=GetTickCount64()+5000;
            const auto logical=std::count_if(scene.marks.begin(),scene.marks.end(),[](const auto& m){return m.id!=0;});
            const auto fading=std::count_if(scene.marks.begin(),scene.marks.end(),[](const auto& m){return m.id && m.opacity()<1;});
            const auto& stats=scene.stats;
            logging::log(logging::Level::debug,logging::Channel::skater,
                "Blood surface: logical {}, owned {} (not visible count), attached {}, fading {}, failed {}; spawned {}, density rejected {}, pressure rejected {}, retirement starts {}, retired {}, expired {}, neighbour checks {}, budget ready {}.",
                logical,owned,s.pool.attached(),fading,failed,stats.spawned,stats.density_rejected,stats.pressure_rejected,stats.retirement_started,stats.retired,stats.expired,stats.neighbour_checks,s.budget_ready);
        }
        if (!s.budget_ready) return {false,owned,"Ground blood may be incomplete: the decal draw allowance is unavailable."};
        if (failed) return {false,owned,"Some ground blood marks could not be created."};
        return {true,owned,"Ground trails are ready."};
    } catch (...) {return {false,0,"Ground trails could not be updated."};}
}
void stop_native_blood_ground() noexcept {
    try {
        auto& s=state(); std::lock_guard lock(s.mutex);
        if (s.thread && s.thread!=GetCurrentThreadId()) {s.stop_requested=true; return;}
        if (s.valid && s.base && s.manager && decal::manager(s.base)==s.manager) s.pool.clear(functions(s));
        else s.pool={};
        if (s.base) {s.budget_at=0; budget(s,false);}
        s.materials={}; s.world=s.generation=0;
    } catch (...) {}
}
}
