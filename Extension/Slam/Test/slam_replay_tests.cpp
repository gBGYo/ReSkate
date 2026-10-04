#include "Extension/Slam/slam_replay.h"
#include "Extension/Slam/slam_replay_native.h"
#include "Engine/Game/Multiplayer/session_tools.h"
#include <iostream>
#include <limits>
#include <unordered_map>

namespace {
using namespace dingosdk::slam;
int failures{};
void check(bool value,const char* message) {
    if (!value) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
void timeline() {
    ReplayVisualHistory history;
    Result result; result.phase=Phase::attempt;
    VisualEvents events;
    for (int i=0;i<10;++i) history.record(i*.1,result,events);
    result.phase=Phase::bailed; result.bone_injuries[103]={250,0,true};
    events.observe(result,700000);
    history.record(1,result,events);
    for (int i=11;i<=30;++i) history.record(i*.1,result,events);
    auto before=history.sample(.99),impact=history.sample(1),later=history.sample(2);
    check(before && !before->result.bone_injuries[103].fractured,"Seeking before a fracture removes its injury");
    check(impact && impact->result.bone_injuries[103].fractured,"Seeking to the fracture restores it");
    check(impact && impact->fracture_seeds[103]==700000,"Fracture geometry keeps its live event identifier");
    check(impact && impact->events.fractures_at_ms[103]==1001,"Fracture animation is keyed to native replay time");
    check(history.size()==2,"Unchanged injury states share bounded coverage rather than allocating per tick");
    VisualOptions options;
    const auto flash=visual_appearance(options,impact->result,impact->events,1001,false,true);
    const auto faded=visual_appearance(options,later->result,later->events,2001,false,true);
    check(flash.damage[103][3]>0 && faded.damage[103][3]==0,"Fracture opening fades on the replay clock");
    const auto paused=history.sample(1);
    check(paused && paused->events.fractures_at_ms==impact->events.fractures_at_ms,"Repeated paused samples do not advance effects");
    result.phase=Phase::ready; result.bone_injuries={}; events.reset();
    history.record(3.1,result,events);
    check(history.sample(3.1) && !history.sample(3.1)->result.bone_injuries[103].fractured,"Recovery clears injuries forward in the timeline");
    check(history.sample(2)->result.bone_injuries[103].fractured,"Rewinding after recovery restores the earlier injury");
    history.record(8,result,events);
    check(!history.sample(6),"Telemetry gaps never carry injuries into unknown recording ranges");
    check(!history.sample(8.1),"Unrecorded future frames have no injury metadata");
    history.record(.1,result,events);
    check(!history.sample(2),"A recording clock reset invalidates the old epoch");
    history.reset();
    check(!history.sample(.1),"Changing actor, map or stream can clear retained metadata");
    for (int i=0;i<10000;++i) {
        result.bone_injuries[103].severity=static_cast<float>(i);
        history.record(i*.01,result,events);
    }
    check(history.size()==ReplayVisualHistory::capacity && !history.sample(1),"Many injury changes remain within the memory cap");
    history.reset();
    for (int i=0;i<7000;++i) history.record(i*.1,result,events);
    check(!history.sample(1) && history.sample(699),"A long unchanged state still obeys the retention window");
    history.record(std::numeric_limits<double>::quiet_NaN(),result,events);
    check(!history.sample(-1) && !history.sample(std::numeric_limits<double>::infinity()),"Invalid replay times are rejected");
    history.reset();
    events.impacts_at_ms[103]=50000; events.latest_impact_ms=50000;
    history.record(10,result,events,52000);
    check(history.sample(10)->events.latest_impact_ms==8001,"Enabling metadata after an old impact preserves its age");
}
struct Memory {
    std::unordered_map<std::uintptr_t,std::byte> bytes;
    template<class T> void set(std::uintptr_t at,const T& value) {
        const auto* source=reinterpret_cast<const std::byte*>(&value);
        for (std::size_t i=0;i<sizeof(value);++i) bytes[at+i]=source[i];
    }
    bool read(std::uintptr_t at,void* value,std::size_t size) const {
        auto* target=static_cast<std::byte*>(value);
        for (std::size_t i=0;i<size;++i) {
            const auto found=bytes.find(at+i);
            if (found==bytes.end()) return false;
            target[i]=found->second;
        }
        return true;
    }
};
void native_clock() {
    namespace r=dingosdk::game::build::v20260929::replay;
    constexpr std::uintptr_t base=0x140000000,client=0x100000,manager=0x200000,backend=0x300000,
        recorder=0x400000,stream=0x500000,interval=0x600000,session=0x700000;
    Memory memory;
    memory.set(base+r::client,client); memory.set(client+r::manager,manager);
    memory.set(manager+r::backend,backend); memory.set(backend,base+r::backend_vtable);
    memory.set(base+r::recorder,recorder); memory.set(base+r::stream,stream);
    memory.set(recorder+r::intervals_begin,interval); memory.set(recorder+r::intervals_end,interval+40);
    memory.set(interval,std::array<std::byte,40>{});
    memory.set(interval,10.); memory.set(interval+16,.02); memory.set(interval+32,std::uint32_t{50});
    memory.set(manager+r::lease,std::uintptr_t{}); memory.set(base+r::playing,std::uint8_t{});
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {return memory.read(at,out,size);};
    auto clock=read_replay_clock(base,read);
    check(clock && !clock->playback && clock->time==11,"Recording time uses interval start, step and count");
    memory.set(manager+r::lease,session); memory.set(session,base+r::lease_vtable); memory.set(session+16,manager);
    memory.set(base+r::playing,std::uint8_t{1}); memory.set(base+r::playhead,10.25);
    constexpr auto export_state=r::export_sequence,export_time=export_state+r::export_time,
        export_index=export_state+r::export_index,export_status=export_state+r::export_status;
    memory.set(base+export_state,std::array<std::byte,r::export_header_size>{});
    clock=read_replay_clock(base,read);
    check(clock && clock->playback && clock->time==10.25 && clock->recorded_until==11,"Playback reads the playhead independently of recording end");
    memory.set(base+r::playhead,10.1);
    check(read_replay_clock(base,read)->time==10.1,"Reverse seeks are read without accumulating elapsed wall time");
    // The preview stays after the impact while a trimmed video is exported
    // from an earlier time. The native export cursor advances independently.
    ReplayVisualHistory history;
    Result result; result.phase=Phase::attempt; VisualEvents events;
    for (int i=0;i<=10;++i) {
        if (i==5) {result.phase=Phase::bailed; result.bone_injuries[103]={250,0,true}; events.observe(result,700000);}
        history.record(10+i*.1,result,events,700000);
    }
    memory.set(base+r::playhead,10.75);
    memory.set(base+export_state,std::uintptr_t{0x800000});
    memory.set(base+export_state+r::export_count,std::uint32_t{1});
    memory.set(base+export_status,std::uint8_t{1});
    for (double time : {10.25,10.5,10.75,10.95,10.25}) {
        memory.set(base+export_time,time);
        clock=read_replay_clock(base,read);
        check(clock && clock->exporting && clock->time==time,"Export uses the native frame cursor instead of the parked editor playhead");
        const auto frame=clock ? history.sample(clock->time) : std::nullopt;
        check(frame && frame->result.bone_injuries[103].fractured==(time>=10.5),
            "Exported bones become damaged at impact and rewind to neutral on a new export");
        if (frame) {
            const auto appearance=visual_appearance(VisualOptions{},frame->result,frame->events,
                ReplayVisualHistory::milliseconds(clock->time),false,true);
            check(appearance.damage[103][0]==(time>=10.5 ? 1.f : 0.f),"Export fracture marks follow the rendered frame's injury state");
            check((appearance.damage[103][3]>0)==(time>=10.5 && time<10.85),"Export fracture effects age on the native frame cursor");
        }
    }
    memory.set(base+export_time,10.75); memory.set(base+export_status,std::uint8_t{2});
    memory.set(base+export_index,std::uint32_t{1});
    check(read_replay_clock(base,read)->time==10.75,"The last export frame keeps its cursor until native export cleanup");
    memory.set(base+export_status,std::uint8_t{0}); memory.set(base+export_time,10.25);
    clock=read_replay_clock(base,read);
    check(clock && !clock->exporting && clock->time==10.75,"Finishing export restores the editor playhead without retaining its cursor");
    memory.set(base+export_status,std::uint8_t{1}); memory.set(base+export_index,std::uint32_t{});
    memory.set(base+export_time,std::numeric_limits<double>::quiet_NaN());
    check(!read_replay_clock(base,read),"An invalid export cursor cannot fall back to damage from the preview");
    memory.set(base+export_time,10.25); memory.set(base+export_state,std::uintptr_t{});
    check(!read_replay_clock(base,read),"A missing export sequence cannot authorize its frame cursor");
    memory.set(base+export_state,std::uintptr_t{0x800000}); memory.set(base+export_index,std::uint32_t{1});
    check(!read_replay_clock(base,read),"An out-of-range active export index cannot publish injuries");
    memory.set(base+export_index,std::uint32_t{});
    unsigned export_copies{};
    auto export_torn=[&](std::uintptr_t at,void* out,std::size_t size) {
        if (at==base+export_state && ++export_copies==2) memory.set(base+export_time,10.5);
        return memory.read(at,out,size);
    };
    check(!read_replay_clock(base,export_torn),"Concurrent export cursor changes cannot publish a torn frame time");
    memory.set(base+export_status,std::uint8_t{0});
    memory.set(session+16,stream);
    check(!read_replay_clock(base,read),"An unrelated session lease cannot authorize playback");
    memory.set(session+16,manager); memory.set(base+r::playhead,std::numeric_limits<double>::quiet_NaN());
    check(!read_replay_clock(base,read),"Nonfinite native playheads fail closed");
    memory.set(base+r::playhead,10.1); memory.set(backend,std::uintptr_t{});
    check(!read_replay_clock(base,read),"Unsupported replay backends fail closed");
    memory.set(backend,base+r::backend_vtable);
    unsigned copies{};
    auto torn=[&](std::uintptr_t at,void* out,std::size_t size) {
        if (at==interval && ++copies==2) memory.set(interval+32,std::uint32_t{51});
        return memory.read(at,out,size);
    };
    check(!read_replay_clock(base,torn),"Concurrent recorder changes cannot publish a torn clock");
}
void replay_actor() {
    namespace e=dingosdk::game::build::v20260929::engine;
    constexpr std::uintptr_t base=0x140000000,client=0x100000,world=0x200000,manager=0x300000,
        entries=0x400000,player=0x500000,entity=0x600000,handle=0x700000,collection=0x800000;
    Memory memory;
    memory.set(client,base+e::client_vtable); memory.set(client+8,world);
    memory.set(base+e::context_player_manager_offset,unsigned{0x100});
    memory.set(world+0x100,manager); memory.set(manager,base+e::local_player_manager_vtable);
    memory.set(manager+0x4c8,entries); memory.set(manager+0x4d0,entries+8); memory.set(entries,player);
    memory.set(player,base+e::local_player_vtable); memory.set(player+0x78,world);
    memory.set(player+0x45,std::uint8_t{1}); memory.set(player+0x44,std::uint8_t{});
    memory.set(player+0xb8,entity); memory.set(player+0xb0,handle); memory.set(handle,entity+8);
    memory.set(entity,base+e::skater_entity_vtable); memory.set(entity+0x20,world);
    memory.set(entity+0xf8,player); memory.set(entity+0x70,collection); memory.set(collection,entity);
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {return memory.read(at,out,size);};
    for (const bool multiplayer : {false,true}) {
        dingosdk::set_multiplayer_session_active(multiplayer);
        const auto actor=read_replay_actor(base,client,read);
        check(actor && actor->base==base && actor->entity==entity && actor->world==world,
            "Solo and multiplayer replay bind the verified local skater without live physics");
        check(actor && !actor->core && !actor->context && !actor->rig && !actor->selector,
            "Replay visual ownership cannot authorize gameplay actions");
    }
    memory.set(player+0x44,std::uint8_t{1});
    check(!read_replay_actor(base,client,read),"A remote multiplayer skater cannot own the replay X-ray");
    memory.set(player+0x44,std::uint8_t{}); memory.set(player+0x45,std::uint8_t{});
    check(!read_replay_actor(base,client,read),"Replay X-rays require the local player flag");
    memory.set(player+0x45,std::uint8_t{1}); memory.set(entity+0xf8,manager);
    check(!read_replay_actor(base,client,read),"A stale player backlink cannot authorize a replay X-ray");
    memory.set(entity+0xf8,player); memory.set(handle,collection);
    check(!read_replay_actor(base,client,read),"A stale entity handle cannot authorize a replay X-ray");
    memory.set(handle,entity+8); memory.set(collection,player);
    check(!read_replay_actor(base,client,read),"A different pose collection cannot authorize a replay X-ray");
    memory.set(collection,entity); memory.set(manager+0x4d0,entries+16);
    check(!read_replay_actor(base,client,read),"An ambiguous local player list cannot authorize a replay X-ray");
    dingosdk::set_multiplayer_session_active(false);
}
void settings() {
    VisualOptions options; options.replay=false;
    check(decode_visual_options(encode_visual_options(options))==std::optional(options),"Replay display preference saves and restores");
    auto document=encode_visual_options(options);
    const auto start=document.find("\"replay\":false,");
    if (start!=std::string::npos) document.erase(start,15);
    check(decode_visual_options(document) && decode_visual_options(document)->replay,"Older visual saves enable replay by default");
}
void render_identity() {
    namespace r=dingosdk::game::build::v20260929::replay;
    constexpr std::uintptr_t base=0x140000000,object=0x800000,source=0x900000;
    constexpr std::uint64_t key=0x040000000000365f;
    Memory memory;
    memory.set(object+r::instance_source,source); memory.set(source,base+r::skinned_state_vtable);
    memory.set(source+r::source_instance,object); memory.set(source+r::source_key,key);
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {return memory.read(at,out,size);};
    check(read_replay_render_key(base,object,read)==key,"Replay rendering retains the recorded StateStream identity");
    memory.set(object+0x108,std::uint32_t{13937});
    check(read_replay_render_key(base,object,read)==key,"Rebuilt allocation indices cannot change visual ownership");
    memory.set(source+r::source_key,key+1);
    check(read_replay_render_key(base,object,read)!=key,"A reused instance belonging to another actor cannot match the owned key");
    memory.set(source+r::source_instance,source);
    check(!read_replay_render_key(base,object,read),"Broken render-instance backlinks fail closed");
    memory.set(source+r::source_instance,object); memory.set(source,std::uintptr_t{});
    check(!read_replay_render_key(base,object,read),"Unrelated native state types cannot establish a render identity");
}
}
int main() {
    timeline(); native_clock(); replay_actor(); settings(); render_identity();
    return failures ? 1 : 0;
}
