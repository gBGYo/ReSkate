#pragma once
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/replay.h"
#include "Extension/Skater/no_bail.h"
#include <array>
#include <cmath>
#include <cstring>
#include <optional>

namespace dingosdk::slam {
struct ReplayClock {
    std::uintptr_t manager{}, recorder{}, stream{}, session{};
    double time{}, recorded_until{};
    bool playback{}, exporting{};
};
// Solo and multiplayer replays share the local player binding. Resolve it
// without requiring live physics, which is suspended in the replay editor.
template<class Read> std::optional<LocalBailOwner> read_replay_actor(std::uintptr_t base,std::uintptr_t client,Read&& read) {
    namespace e=game::build::v20260929::engine;
    const auto get=[&]<class T>(std::uintptr_t at,T& out) {return read(at,&out,sizeof(out));};
    const auto pointer=[&](std::uintptr_t at) {
        std::uintptr_t out{};
        return get(at,out) && out>=0x10000 && out<=0x00007ffffffeffffULL ? out : 0;
    };
    unsigned offset{};
    if (pointer(client)!=base+e::client_vtable || !get(base+e::context_player_manager_offset,offset) || offset>0x1000000) return {};
    const auto world=pointer(client+8),manager=pointer(world+offset);
    const auto begin=pointer(manager+0x4c8),end=pointer(manager+0x4d0);
    if (!world || pointer(manager)!=base+e::local_player_manager_vtable || !begin || end!=begin+8) return {};
    const auto player=pointer(begin),entity=pointer(player+0xb8);
    std::uint8_t local{},remote{};
    if (pointer(player)!=base+e::local_player_vtable || pointer(player+0x78)!=world ||
        !get(player+0x45,local) || local!=1 || !get(player+0x44,remote) || remote ||
        pointer(entity)!=base+e::skater_entity_vtable || pointer(entity+0x20)!=world || pointer(entity+0xf8)!=player ||
        pointer(pointer(player+0xb0))!=entity+8 || pointer(pointer(entity+0x70))!=entity) return {};
    // A visual identity carries no physics core, rig or selector and cannot
    // authorize a bail, teleport or telemetry capture.
    return LocalBailOwner{base,entity,world,0,0,0,0};
}
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
    std::array<std::byte,r::export_header_size> capture{},capture_after{};
    if (out.playback) {
        if (!read(base+r::export_sequence,capture.data(),capture.size())) return {};
        const auto status=std::to_integer<unsigned>(capture[r::export_status]);
        if (status>2) return {};
        out.exporting=status!=0;
        if (out.exporting) {
            std::uintptr_t segments{}; std::uint32_t segments_count{},index{};
            std::memcpy(&segments,capture.data(),sizeof(segments));
            std::memcpy(&segments_count,capture.data()+r::export_count,sizeof(segments_count));
            std::memcpy(&index,capture.data()+r::export_index,sizeof(index));
            if (segments<0x10000 || segments>=0x00007fffffff0000ULL || (segments&7) ||
                !segments_count || segments_count>1000000 || index>segments_count || (status==1 && index==segments_count)) return {};
            std::memcpy(&out.time,capture.data()+r::export_time,sizeof(out.time));
        } else if (!get(base+r::playhead,out.time)) return {};
    }
    if (!std::isfinite(out.time) || out.time<0 || out.time>1e9 || !std::isfinite(out.recorded_until) || out.recorded_until>1e9 ||
        ptr(base+r::client)!=client || ptr(client+r::manager)!=out.manager || ptr(base+r::recorder)!=out.recorder ||
        ptr(base+r::stream)!=out.stream || ptr(out.manager+r::lease)!=out.session ||
        ptr(out.recorder+r::intervals_begin)!=begin || ptr(out.recorder+r::intervals_end)!=end ||
        (out.playback && (!read(base+r::export_sequence,capture_after.data(),capture_after.size()) || capture!=capture_after))) return {};
    return out;
}
}
