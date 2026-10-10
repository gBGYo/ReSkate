#include "blood_native.h"
#include "Engine/Game/Build/20260929/blood.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Abi/native_data.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Core/Log/logging.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace dingosdk::blood {
namespace {
namespace layout=game::build::v20260929::blood;
struct NativeState {
    std::mutex mutex;
    BloodNativePool pool;
    std::uintptr_t base{},service{};
    std::uint64_t world{},generation{},lookup_at{};
    std::array<std::uintptr_t,blood_asset_names.size()> assets{};
    BloodColor color=BloodColor::red;
    DWORD thread{};
    bool checked{},valid{},stop_requested{};
};
NativeState& native() {static auto* s=new NativeState; return *s;}
std::uintptr_t create(void* context,BloodKind kind,BloodIntensity intensity,const std::array<float,16>& transform) {
    auto& s=*static_cast<NativeState*>(context);
    const auto index=blood_asset_index(kind,intensity);
    if (index>=s.assets.size()) return 0;
    const auto asset=s.assets[index];
    if (!asset) return 0;
    alignas(16) std::array<std::byte,0x100> descriptor{};
    // Initializer retains the blueprint; the matching destructor releases it
    // and any descriptor-owned arrays after synchronous creation.
    reinterpret_cast<void(*)(void*,std::uintptr_t,const float*)>(s.base+layout::descriptor_init.rva)
        (descriptor.data(),asset,transform.data());
    const std::uint32_t flags=0x20;
    std::memcpy(descriptor.data()+0xc0,&flags,sizeof(flags));
    std::uintptr_t handle{};
    reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t*,void*)>(s.base+layout::create.rva)
        (0,&handle,descriptor.data());
    reinterpret_cast<void(*)(void*)>(s.base+layout::descriptor_destroy.rva)(descriptor.data());
    logging::log(logging::Level::debug,logging::Channel::skater,
        "Blood: created kind {}, intensity {}, asset {}, handle {:x}, at ({:.3f}, {:.3f}, {:.3f}), launch ({:.3f}, {:.3f}, {:.3f}).",
        static_cast<unsigned>(kind),static_cast<unsigned>(intensity),blood_asset_names[index],handle,
        transform[12],transform[13],transform[14],transform[4],transform[5],transform[6]);
    return handle;
}
void move(void* context,std::uintptr_t& handle,const std::array<float,16>& transform) {
    const auto& s=*static_cast<NativeState*>(context);
    reinterpret_cast<void(*)(std::uintptr_t*,const float*)>(s.base+layout::move.rva)(&handle,transform.data());
}
void stop(void* context,std::uintptr_t& handle,bool kill) {
    const auto& s=*static_cast<NativeState*>(context);
    reinterpret_cast<void(*)(std::uintptr_t*,bool,float)>(s.base+layout::stop.rva)(&handle,kill,0);
    logging::log(logging::Level::debug,logging::Channel::skater,"Blood: stopped handle {:x}, kill {}.",handle,kill);
}
void release(void* context,std::uintptr_t& handle) {
    const auto& s=*static_cast<NativeState*>(context);
    const auto previous=handle;
    reinterpret_cast<void(*)(std::uintptr_t*)>(s.base+layout::release.rva)(&handle);
    logging::log(logging::Level::debug,logging::Channel::skater,"Blood: released handle {:x}, remaining {:x}.",previous,handle);
}
BloodNativeFunctions functions(NativeState& s) {return {&s,create,move,stop,release};}
bool contracts(std::uintptr_t base) {
    if (!base) return false;
    for (const auto& f:{layout::descriptor_init,layout::descriptor_destroy,layout::create,layout::move,layout::stop,layout::release}) {
        std::array<unsigned char,32> bytes{};
        if (!memory::read(base+f.rva,bytes) || bytes!=f.bytes) return false;
    }
    return true;
}
std::uintptr_t service(std::uintptr_t base) {
    std::uintptr_t instance{},vtable{};
    if (!memory::read(base+layout::service,instance) || !instance || !memory::read(instance,vtable)) return 0;
    for (const auto slot:{0x10u,0x20u,0x28u,0x30u}) {
        std::uintptr_t method{};
        if (!memory::read(vtable+slot,method) || method<base || method>=base+supported_build::game_image_size) return 0;
    }
    return instance;
}
std::uintptr_t find_asset(std::uintptr_t base,const char* name) {
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
BloodStatus update_native_blood(std::uintptr_t base,const BloodScene& scene,bool allowed,bool ground_allowed) noexcept {
    const auto ground=update_native_blood_ground(base,scene.ground,ground_allowed);
    try {
        auto& s=native(); std::lock_guard lock(s.mutex);
        if (!s.thread) s.thread=GetCurrentThreadId();
        if (s.thread!=GetCurrentThreadId()) return {false,s.pool.active(),"Waiting for the game thread."};
        if (!s.checked) {s.base=base; s.valid=contracts(base); s.checked=true;}
        if (!s.valid || base!=s.base) return {false,0,"Blood effects are unavailable for this game build."};
        const auto current=service(base);
        // The service owns its pool. References from a destroyed service must
        // never be submitted to its replacement. Normal loading clears them
        // through stop_native_blood before the scene is destroyed.
        if (s.service && s.service!=current) {
            s.pool=BloodNativePool{}; s.assets={}; s.world=s.generation=0;
        }
        s.service=current;
        if (!current) return {false,0,"Blood effects are waiting for the world."};
        const auto f=functions(s);
        if (s.stop_requested) {s.pool.clear(f); s.assets={}; s.world=s.generation=0; s.stop_requested=false;}
        if (!allowed || !scene.entity || !scene.world) {
            s.pool.clear(f); s.assets={}; s.world=s.generation=0;
            return {ground.available,0,ground.available ? "Blood effects and ground trails are ready." : ground.detail,ground.active};
        }
        if (s.world!=scene.world || s.generation!=scene.generation || s.color!=scene.color) {
            s.pool.clear(f); s.assets={}; s.lookup_at=0; s.world=scene.world; s.generation=scene.generation; s.color=scene.color;
        }
        const auto missing=[&]{return std::any_of(s.assets.begin(),s.assets.end(),[](auto asset){return !asset;});};
        if (missing()) {
            if (GetTickCount64()>=s.lookup_at) {
                s.lookup_at=GetTickCount64()+2000;
                for (std::size_t i=0;i<s.assets.size();++i) s.assets[i]=find_asset(base,blood_colored_asset(blood_asset_names[i],scene.color).c_str());
            }
            if (missing()) return {false,0,"Selected blood color assets are not installed. Update the blood mod."};
        }
        s.pool.update(scene,f,true);
        return {ground.available,s.pool.active(),ground.available ? "Blood effects and ground trails are ready." : ground.detail,ground.active};
    } catch (...) {return {false,0,"Blood effects could not be updated."};}
}
void stop_native_blood() noexcept {
    stop_native_blood_ground();
    try {
        auto& s=native(); std::lock_guard lock(s.mutex);
        if (s.thread && s.thread!=GetCurrentThreadId()) {s.stop_requested=true; return;}
        if (s.valid && s.base && s.service && service(s.base)==s.service) s.pool.clear(functions(s));
        else s.pool=BloodNativePool{};
        s.assets={}; s.world=s.generation=0;
    } catch (...) {}
}
}
