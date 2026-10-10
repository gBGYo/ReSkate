#include "blood_native.h"
#include <algorithm>

namespace dingosdk::blood {
void BloodNativePool::clear(const BloodNativeFunctions& f) {
    for (auto& e:entries_) {
        if (e.handle) {f.stop(f.context,e.handle,true); f.release(f.context,e.handle);}
        e={};
    }
    entity_=world_=generation_=0;
}
std::size_t BloodNativePool::active() const noexcept {
    return std::count_if(entries_.begin(),entries_.end(),[](const auto& e){return e.handle!=0;});
}
void BloodNativePool::update(const BloodScene& scene,const BloodNativeFunctions& f,bool allowed) {
    if (!f.create || !f.move || !f.stop || !f.release) return;
    if (!allowed || entity_!=scene.entity || world_!=scene.world || generation_!=scene.generation) clear(f);
    if (!allowed || !scene.entity || !scene.world) return;
    entity_=scene.entity; world_=scene.world; generation_=scene.generation;
    for (auto& e:entries_) {
        if (!e.id) continue;
        const auto source=std::find_if(scene.sources.begin(),scene.sources.end(),[&](const auto& s){return s.id==e.id;});
        if (source==scene.sources.end()) {
            if (e.handle) {f.stop(f.context,e.handle,true); f.release(f.context,e.handle);}
            e={};
        }
    }
    for (const auto& s:scene.sources) {
        if (!s.id) continue;
        auto e=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.id==s.id;});
        if (e==entries_.end()) {
            // Do not replay a delayed contact after its emission window.
            if (!s.emitting) continue;
            e=std::find_if(entries_.begin(),entries_.end(),[](const auto& e){return !e.id;});
            if (e==entries_.end()) continue;
            e->id=s.id; e->handle=f.create(f.context,s.kind,s.intensity,s.transform);
            // Failed creation is remembered until this source expires. An
            // unavailable asset never causes a creation loop every frame.
            continue;
        }
        if (!e->handle) continue;
        if (!s.emitting && !e->stopped) {f.stop(f.context,e->handle,false); e->stopped=true;}
        if (s.emitting && !e->stopped && s.kind!=BloodKind::spray) f.move(f.context,e->handle,s.transform);
    }
}
}
