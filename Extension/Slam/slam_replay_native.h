#pragma once
#include "Engine/Game/Build/20260929/replay.h"
#include <array>
#include <cmath>
#include <cstring>
#include <optional>

namespace dingosdk::slam {
struct ReplayClock {
    std::uintptr_t manager{}, recorder{}, stream{}, session{};
    double time{}, recorded_until{};
    bool playback{};
};
template<class Read> std::optional<std::uint64_t> read_replay_render_key(std::uintptr_t base,std::uintptr_t object,Read&& read) {
    namespace r=game::build::v20260929::replay;
    std::uintptr_t source{},vtable{},backlink{},verified{}; std::uint64_t key{},after{};
    if (!read(object+r::instance_source,&source,sizeof(source)) || source<0x10000 || source>0x00007fffffff0000ULL || (source&7) ||
        !read(source,&vtable,sizeof(vtable)) || vtable!=base+r::skinned_state_vtable ||
        !read(source+r::source_instance,&backlink,sizeof(backlink)) || backlink!=object ||
        !read(source+r::source_key,&key,sizeof(key)) || !key ||
        !read(object+r::instance_source,&verified,sizeof(verified)) || verified!=source ||
        !read(source+r::source_key,&after,sizeof(after)) || after!=key) return {};
    return key;
}
// Read-only and injectable: no native calls, locks, seeks or replay mutations.
template<class Read> std::optional<ReplayClock> read_replay_clock(std::uintptr_t base,Read&& read) {
    namespace r=game::build::v20260929::replay;
    const auto get=[&]<class T>(std::uintptr_t at,T& out) {return read(at,&out,sizeof(out));};
    const auto ptr=[&](std::uintptr_t at) {
        std::uintptr_t out{};
        return get(at,out) && out>=0x10000 && out<0x00007fffffff0000ULL && !(out&7) ? out : 0;
    };
    ReplayClock out;
    const auto client=ptr(base+r::client);
    out.manager=ptr(client+r::manager); out.recorder=ptr(base+r::recorder); out.stream=ptr(base+r::stream);
    const auto backend=ptr(out.manager+r::backend);
    if (!client || !out.manager || !out.recorder || !out.stream || !backend || ptr(backend)!=base+r::backend_vtable) return {};
    out.session=ptr(out.manager+r::lease);
    std::uint8_t playing{};
    if (!get(base+r::playing,playing) || playing>1) return {};
    out.playback=out.session!=0;
    if ((playing!=0)!=out.playback) return {};
    if (out.playback && (!playing || ptr(out.session)!=base+r::lease_vtable || ptr(out.session+r::lease_manager)!=out.manager)) return {};
    // Double-copy the interval and its vector header. Recording can append or
    // update it concurrently; torn observations never enter the history.
    const auto begin=ptr(out.recorder+r::intervals_begin),end=ptr(out.recorder+r::intervals_end);
    if (!begin || end<=begin || end-begin>r::interval_stride*1000000 || (end-begin)%r::interval_stride) return {};
    std::array<std::byte,r::interval_stride> interval{},verified{};
    if (!read(end-r::interval_stride,interval.data(),interval.size()) ||
        !read(end-r::interval_stride,verified.data(),verified.size()) || interval!=verified) return {};
    double start{},step{}; std::uint32_t count{};
    std::memcpy(&start,interval.data(),8); std::memcpy(&step,interval.data()+16,8); std::memcpy(&count,interval.data()+32,4);
    if (!std::isfinite(start) || start<0 || !std::isfinite(step) || step<=0 || step>1) return {};
    out.recorded_until=start+step*count;
    out.time=out.recorded_until;
    if (out.playback && !get(base+r::playhead,out.time)) return {};
    if (!std::isfinite(out.time) || out.time<0 || out.time>1e9 || !std::isfinite(out.recorded_until) || out.recorded_until>1e9 ||
        ptr(base+r::client)!=client || ptr(client+r::manager)!=out.manager || ptr(base+r::recorder)!=out.recorder ||
        ptr(base+r::stream)!=out.stream || ptr(out.manager+r::lease)!=out.session ||
        ptr(out.recorder+r::intervals_begin)!=begin || ptr(out.recorder+r::intervals_end)!=end) return {};
    return out;
}
}
