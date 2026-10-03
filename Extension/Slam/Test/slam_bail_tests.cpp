// Exercise the actual shared-hook bridge against owned in-process native data.
// No game executable or copyrighted assets are needed by this fixture.
#include "Extension/Skater/no_bail.cpp"
#include "Extension/Slam/slam_controls.h"
#include <iostream>
#include <stdexcept>

namespace fixture {
int failures{},selection_calls{},publication_calls{},resets{};
bool animation_request{},entry_request{},response_request{},full_animation_request{},full_response_request{};
std::uint32_t observed{};
void check(bool ok,const char* text) {if (!ok) {++failures; std::cerr<<"FAIL: "<<text<<'\n';}}
template<class T> void put(std::uintptr_t at,T value) {std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uint32_t choose(std::uintptr_t selector,std::uint32_t current) {
    ++selection_calls;
    const auto context=*reinterpret_cast<std::uintptr_t*>(selector+8);
    const auto flags=*reinterpret_cast<std::uint32_t*>(context+dingosdk::addr::no_bail::impact_request_offset);
    return current!=504 && (flags&dingosdk::addr::no_bail::impact_request_mask) ? 300 : current;
}
void transition(std::uintptr_t core) {
    const auto context=*reinterpret_cast<std::uintptr_t*>(core+0x3c0);
    const auto selector=*reinterpret_cast<std::uintptr_t*>(core+0x440);
    const auto current=*reinterpret_cast<std::uint32_t*>(context+0x1414);
    const auto next=dingosdk::choose_state(selector,current);
    entry_request=(*reinterpret_cast<std::uint32_t*>(context+dingosdk::addr::no_bail::impact_request_offset)&0x8000)!=0;
    if (current!=next) put<std::uint32_t>(context+0x1414,next==300 ? 504 : next);
}
void response(std::uintptr_t rig,float,bool wipeout) {
    const auto context=*reinterpret_cast<std::uintptr_t*>(rig);
    response_request=(*reinterpret_cast<std::uint32_t*>(context+dingosdk::addr::no_bail::impact_request_offset)&0x8000)!=0;
    full_response_request=wipeout &&
        (*reinterpret_cast<std::uint32_t*>(context+dingosdk::addr::no_bail::animation_request_offset)&0x08000000)!=0;
}
void publish(std::uintptr_t core) {
    ++publication_calls;
    const auto context=*reinterpret_cast<std::uintptr_t*>(core+0x3c0);
    auto& flags=*reinterpret_cast<std::uint32_t*>(context+dingosdk::addr::no_bail::impact_request_offset);
    animation_request=(flags&dingosdk::addr::no_bail::impact_request_mask)!=0;
    full_animation_request=(*reinterpret_cast<std::uint32_t*>(context+dingosdk::addr::no_bail::animation_request_offset)&0x08000000)!=0;
    flags|=0x2000; // An unrelated native write during the consumer must survive.
}
void reset(std::uintptr_t) {++resets;}
void enter(std::uintptr_t child,std::uintptr_t parent,std::uintptr_t) {
    const auto context=*reinterpret_cast<std::uintptr_t*>(parent+8);
    put<std::array<float,4>>(child+0x10,*reinterpret_cast<std::array<float,4>*>(context+0x8f0));
}
struct Scene {
    std::uintptr_t image{},objects{},client{},world{},entity{},player{},core{},context{},selector{};
    Scene() {
        using namespace dingosdk;
        image=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,supported_build::game_image_size,MEM_RESERVE,PAGE_READWRITE));
        objects=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x30000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if (!image || !objects) throw std::runtime_error("Cannot allocate bail fixture");
        const auto global=image+addr::engine::context_player_manager_offset;
        if (!VirtualAlloc(reinterpret_cast<void*>(global&~std::uintptr_t{0xfff}),0x1000,MEM_COMMIT,PAGE_READWRITE))
            throw std::runtime_error("Cannot commit fixture manager offset");
        put<unsigned>(global,0x100);
        client=objects+0x1000; world=objects+0x2000; player=objects+0x5000; entity=objects+0x6000;
        core=objects+0xa000; selector=objects+0xb000; context=objects+0xd000;
        const auto manager=objects+0x3000,entries=objects+0x4000,handle=objects+0x7000;
        const auto collection=objects+0x8000,component=objects+0x9000,causes=objects+0xc000,rig=objects+0x10000;
        put(client,image+addr::engine::client_vtable); put(client+8,world); put<unsigned>(client+0xc4,13);
        put(world+0x100,manager); put(manager,image+addr::engine::local_player_manager_vtable);
        put(manager+0x4c8,entries); put(manager+0x4d0,entries+8); put(entries,player);
        put(player,image+addr::engine::local_player_vtable); put(player+0x78,world);
        put<std::uint8_t>(player+0x45,1); put(player+0xb8,entity); put(player+0xb0,handle);
        put(entity,image+addr::engine::skater_entity_vtable); put(entity+0x20,world); put(entity+0xf8,player);
        put(entity+0x70,collection); put(entity+0x628,component); put(handle,entity+8); put(collection,entity);
        put(component,image+addr::engine::skater_component_vtable); put(component+0x18,collection); put(component+0x70,core);
        put(core,image+addr::no_bail::bail_core_vtable); put(core+0x3c0,context); put(core+0x440,selector);
        put(core+0x428,causes); put(core+0x438,rig); put(selector+8,context); put(causes+0x20,context);
        put(rig,context); put(rig+0x4630,core); put<std::uint32_t>(context+0x1414,100);
        auto& p=protection(); p.base=image; p.ready.store(true); p.choose_original=choose; p.publish_original=publish;
        p.transition_original=transition; p.skeleton_original=response; p.ragdoll_enter_original=enter;
        p.reset_causes=reset; p.lease={}; p.manual_bail.cancel();
    }
    ~Scene() {dingosdk::protection().ready.store(false); VirtualFree(reinterpret_cast<void*>(objects),0,MEM_RELEASE); VirtualFree(reinterpret_cast<void*>(image),0,MEM_RELEASE);}
    std::uint32_t& flags() const {return *reinterpret_cast<std::uint32_t*>(context+dingosdk::addr::no_bail::impact_request_offset);}
};
}
namespace dingosdk {
HookStatus WINAPI hook_prepare(void*,void*,void**) {return HookUnsupportedFunction;}
HookStatus WINAPI hook_enable(void*) {return HookUnsupportedFunction;}
HookStatus WINAPI hook_remove(void*) {return HookNotFound;}
namespace slam {
void observe_selection(std::uintptr_t,std::uint32_t next) noexcept {fixture::observed=next;}
void observe_manual_bail(std::uintptr_t) noexcept {}
void observe_skeleton(std::uintptr_t,float,bool) noexcept {}
}
}
int main() {
    using namespace dingosdk;
    using namespace dingosdk::slam;
    using fixture::check;
    fixture::Scene s;
    s.flags()=0x40000000;
    check(queue_manual_bail(s.client,s.entity),"A verified owned skater can queue a deliberate bail");
    check(!queue_manual_bail(s.client,s.entity),"A pending request cannot be duplicated by held input");
    check(choose_state(s.selector,100)==300 && fixture::observed==300,"The original selector receives the request and selects native wipeout");
    check(s.flags()==0x40000000,"The temporary selector flag is restored before returning to native physics");
    publish_animation(s.core);
    check(fixture::animation_request && s.flags()==0x40002000,"Animation consumes the same request without retaining it or discarding unrelated native writes");
    publish_animation(s.core);
    check(fixture::animation_request,"Physics keeps the request visible until animation actually enters ragdoll");
    const auto counts=manual_bail_status();
    check(counts.queued==1 && counts.selected==1 && counts.published==1,"Native selection and publication counters confirm one consumed request");
    cancel_manual_bail(); publish_animation(s.core);
    check(!fixture::animation_request,"Canceling delivery prevents a later publication from replaying the request");
    s.flags()=0x8000;
    check(queue_manual_bail(s.client,s.entity) && choose_state(s.selector,100)==300,"A native impact request can coexist with a manual request");
    publish_animation(s.core);
    check((s.flags()&0x8000)!=0,"SDK cleanup never clears a request already owned by the game");
    cancel_manual_bail();
    s.flags()=0;
    check(update_no_bail(s.client,s.entity,true,false,0) && !queue_manual_bail(s.client,s.entity),"No Bail protection rejects deliberate bails");
    update_no_bail(s.client,s.entity,false,false,0);
    check(queue_manual_bail(s.client,s.entity),"Turning No Bail off permits a fresh request");
    update_no_bail(s.client,s.entity,true,false,0);
    check(choose_state(s.selector,100)==100,"Enabling No Bail after queueing blocks the native request");
    publish_animation(s.core);
    check(!fixture::animation_request && fixture::resets>0,"No Bail also prevents animation publication of the queued request");
    update_no_bail(s.client,s.entity,false,false,0);
    check(queue_manual_bail(s.client,s.entity),"The next normal request can be queued");
    fixture::put<std::uint8_t>(s.entity+0x7e0,1);
    check(choose_state(s.selector,100)==100 && !queue_manual_bail(s.client,s.entity),"Teleport handoff invalidates both queued and new requests");
    fixture::put<std::uint8_t>(s.entity+0x7e0,0);
    fixture::put<std::uint32_t>(s.context+0x1414,504);
    check(queue_manual_bail(s.client,s.entity),"Offboard walking can queue a deliberate bail");
    transition_state(s.core);
    check(fixture::entry_request && fixture::observed==504 &&
        *reinterpret_cast<std::uint32_t*>(s.context+0x1414)==504,
        "An offboard request leaves native physics in 504 instead of inventing a state transition");
    skeleton_response(*reinterpret_cast<std::uintptr_t*>(s.core+0x438),.016f,false);
    check(fixture::response_request && !(s.flags()&0x8000),
        "Skeleton response sees the native impact request without leaving a sticky flag");
    check(fixture::full_response_request &&
        !(*reinterpret_cast<std::uint32_t*>(s.context+0x13d4)&0x08000000),
        "Full wipeout response receives the same bit and bool as its native caller, then restores the borrowed bit");
    publish_animation(s.core);
    check(fixture::animation_request,"Offboard animation receives the deliberate fall request");
    check(fixture::full_animation_request,"Offboard animation receives the independent full wipeout request as well as the impact request");
    publish_animation(s.core);
    check(fixture::animation_request,"Offboard delivery also survives multiple physics publications");
    cancel_manual_bail(); publish_animation(s.core);
    check(!fixture::animation_request,"Canceling an offboard request stops delivery immediately");
    LocalBailOwner walking;
    check(resolve_local_bail_owner(s.client,s.entity,walking),"The walking fixture retains local ownership");
    const auto parent=s.objects+0x16000,board=s.objects+0x15000;
    fixture::put(s.core+0x3a8,parent); fixture::put(s.core+0x3b0,parent); fixture::put(s.core+0x430,board);
    fixture::put(parent,s.image+addr::offboard_flight::offboard_flight_vtable);
    fixture::put(parent+8,s.context); fixture::put(parent+0x10,board); fixture::put(parent+0x18,walking.rig);
    fixture::put(parent+0x48,parent+0x60);
    fixture::put(parent+0x60,s.image+addr::offboard_flight::offboard_flight_states[0].vtable_rva);
    check(local_bail_recovered(walking),"Verified walking ground motion rearms offboard bails");
    fixture::put<std::uint32_t>(s.context+0x13d4,0x08000000);
    check(!local_bail_recovered(walking),"An outstanding native wipeout request cannot be mistaken for recovery");
    fixture::put<std::uint32_t>(s.context+0x13d4,0);
    fixture::put(parent+0x48,parent+0x720);
    check(!local_bail_recovered(walking),"A trajectory or ragdoll substate cannot rearm the shortcut");
    fixture::put(parent+0x48,parent+0x60); fixture::put(parent+0x10,board+0x100);
    check(!local_bail_recovered(walking),"A replaced offboard board wrapper invalidates walking recovery");
    fixture::put(parent+0x10,board);
    const auto physics=s.objects+0x20000,parts=s.objects+0x22000,child=parent+ragdoll_child_offset;
    fixture::put(walking.rig+0x2f10,physics);
    fixture::put(physics,s.image+addr::client_source_spawn::rig_physics_vtable);
    fixture::put(physics+0x20,parts); fixture::put<std::uint32_t>(parts,26);
    fixture::put(child,s.image+ragdoll_child_vtable); fixture::put(parent+0x48,child);
    for (std::size_t i=1;i<=23;++i) {
        fixture::put(parts+i*0x130+0x10,physics);
        fixture::put(parts+i*0x130+0x70,std::array<float,4>{i==1 ? 3.f : 1.f,-2.f,1.f,55.f});
        fixture::put<std::uint32_t>(parts+i*0x130+0x60,0x400);
        fixture::put(parts+i*0x130+0x80,std::array<float,4>{1,2,3,4});
    }
    fixture::put(s.context+0x8f0,std::array<float,4>{6,3,7,42});
    check(queue_manual_bail(s.client,s.entity),"A walking bail can capture forward momentum before animation resets it");
    transition_state(s.core); publish_animation(s.core);
    fixture::put(s.context+0x8f0,std::array<float,4>{0,3,0,42});
    ragdoll_enter(child,parent,parent+0x60);
    check(*reinterpret_cast<std::array<float,4>*>(child+0x10)==std::array<float,4>{6,3,7,42},
        "Confirmed native ragdoll entry restores horizontal trajectory and preserves vertical velocity and W");
    check(*reinterpret_cast<std::array<float,4>*>(parts+23*0x130+0x70)==std::array<float,4>{6,-2,7,55} &&
        *reinterpret_cast<std::array<float,4>*>(parts+0x130+0x70)==std::array<float,4>{8,-2,7,55},
        "One handoff preserves relative limb velocity while restoring horizontal root momentum");
    check(*reinterpret_cast<std::array<float,4>*>(parts+0x130+0x80)==std::array<float,4>{1,2,3,4} &&
        *reinterpret_cast<std::uint32_t*>(parts+0x130+0x60)==0x408,
        "Momentum handoff preserves angular velocity and unrelated flags, and marks native velocity dirty");
    publish_animation(s.core);
    check(!fixture::animation_request,"Actual ragdoll entry acknowledges the bail and stops further request delivery");
    ragdoll_enter(child,parent,parent+0x60);
    check(*reinterpret_cast<std::array<float,4>*>(parts+23*0x130+0x70)==std::array<float,4>{6,-2,7,55},
        "A later entry cannot apply the completed momentum handoff twice");
    Owner momentum_owner;
    check(resolve(s.client,s.entity,momentum_owner),"Momentum handoff retains the full local owner chain");
    Protection::Momentum invalid{momentum_owner,{30,0,20},1,GetTickCount64()+500};
    fixture::put(parts+0x130+0x10,physics+0x100);
    check(!handoff_bail_momentum(invalid,child,parent) &&
        *reinterpret_cast<std::array<float,4>*>(parts+23*0x130+0x70)==std::array<float,4>{6,-2,7,55},
        "A replaced limb owner rejects the complete handoff before any velocities are written");
    fixture::put(parts+0x130+0x10,physics);
    invalid.until=GetTickCount64();
    check(!handoff_bail_momentum(invalid,child,parent),"Expired momentum cannot change a later fall");
    invalid.until=GetTickCount64()+500;
    fixture::put<std::uint8_t>(s.entity+0x7e0,1);
    check(!handoff_bail_momentum(invalid,child,parent),"Teleporting cancels native momentum ownership");
    fixture::put<std::uint8_t>(s.entity+0x7e0,0);
    fixture::put<std::uint32_t>(s.context+0x1414,100);
    check(queue_manual_bail(s.client,s.entity),"A new boarding request can follow offboard publication");
    transition_state(s.core);
    check(fixture::entry_request && fixture::observed==300 && !(s.flags()&0x8000),
        "The request survives native state entry and is restored only after the whole transition");
    publish_animation(s.core);
    cancel_manual_bail();
    fixture::put<std::uint32_t>(s.context+0x1414,400);
    check(queue_manual_bail(s.client,s.entity) && choose_state(s.selector,400)==300,"Airborne boarding states use the same native impact request");
    cancel_manual_bail(); publish_animation(s.core);
    check(!fixture::animation_request,"Cancellation prevents a deferred animation request");
    // A fakie skater faces opposite to travel. The board velocity, not the
    // animation's facing vector, must survive the delayed native fall entry.
    const auto board_physics=s.objects+0x18000,board_parts=s.objects+0x19000;
    fixture::put(board+0x18,board_physics);
    fixture::put(board_physics,s.image+addr::client_source_spawn::board_physics_vtable);
    fixture::put(board_physics+0x20,board_parts); fixture::put<std::uint32_t>(board_parts,9);
    fixture::put(board_parts+0x130+0x10,board_physics);
    fixture::put(board_parts+0x130+0x70,std::array<float,4>{-6,0,-7,99});
    fixture::put(s.context+0x8f0,std::array<float,4>{6,3,7,42});
    fixture::put<std::uint32_t>(s.context+0x1414,100);
    check(queue_manual_bail(s.client,s.entity),"A fakie request queues on the owned board");
    transition_state(s.core); publish_animation(s.core);
    check(protection().momentum.velocity==std::array<float,3>{-6,0,-7},
        "Captured velocity follows world travel even when animation faces backwards");
    const auto pending=protection().manual_bail.ticket();
    check(pending.has_value(),"The delivered bail retains its request ticket");
    if (pending) protection().manual_bail.response(pending->owner,GetTickCount64()+501,true);
    check(!protection().manual_bail.ticket(),"Native request delivery still expires at 500ms");
    ragdoll_enter(child,parent,parent+0x60);
    check(*reinterpret_cast<std::array<float,4>*>(child+0x10)==std::array<float,4>{-6,3,-7,42} &&
        *reinterpret_cast<std::array<float,4>*>(parts+23*0x130+0x70)==std::array<float,4>{-6,-2,-7,55},
        "A delayed ragdoll entry carries fakie travel without extending request flags or altering vertical motion");
    cancel_manual_bail();
    fixture::put<std::uint32_t>(s.context+0x1414,100);
    fixture::put(board_parts+0x130+0x10,board_physics+0x100);
    check(queue_manual_bail(s.client,s.entity),"A replaced board body may still request a native bail");
    transition_state(s.core); publish_animation(s.core);
    check(!protection().momentum.serial,"A replaced board body cannot supply momentum for the local skater");
    cancel_manual_bail();
    const auto registry_global=s.image+bail_context_registry;
    check(VirtualAlloc(reinterpret_cast<void*>(registry_global&~std::uintptr_t{0xfff}),0x1000,
        MEM_COMMIT,PAGE_READWRITE)!=nullptr,"The fixture can own a native context registry");
    const auto vm=s.objects+0x25000,output=s.objects+0x26000,resource=s.objects+0x27000;
    const auto instance=s.objects+0x28000,arguments=s.objects+0x29000;
    const auto registry=s.objects+0x2a000,table=s.objects+0x2b000,row=table+320;
    constexpr std::uint64_t context_key=(1ULL<<62)|1;
    fixture::put(registry_global,registry); fixture::put<std::uint32_t>(registry+0x10,1);
    fixture::put(registry+0x20,table); fixture::put(row,context_key); fixture::put(row+8,s.entity);
    fixture::put(vm+0x30,instance); fixture::put(vm+0x38,resource); fixture::put(instance,resource);
    fixture::put(instance+0x48,arguments); fixture::put(arguments,output+8);
    fixture::put(arguments+16,output); fixture::put(arguments+8,output+4);
    fixture::put(output+8,context_key); fixture::put<std::uint8_t>(output,0); fixture::put(output+4,.3f);
    fixture::put(resource+0x10,request_wipeout_graph_key);
    fixture::put(resource+0x20,request_wipeout_graph_layout);
    fixture::put(resource+request_wipeout_outputs_offset,request_wipeout_outputs);
    fixture::put<std::uint32_t>(s.context+0x1414,504);
    check(queue_manual_bail(s.client,s.entity),"An offboard request can reach the gameplay predicate");
    apply_manual_bail_expression(vm,1);
    check(!*reinterpret_cast<std::uint8_t*>(output),"A partial VM cursor cannot supply a completed predicate result");
    fixture::put(resource+0x10,request_wipeout_graph_key+1);
    apply_manual_bail_expression(vm,0);
    check(!*reinterpret_cast<std::uint8_t*>(output),"Unrelated expressions retain their native result");
    fixture::put(resource+0x10,request_wipeout_graph_key);
    fixture::put(row+8,s.entity+0x100);
    apply_manual_bail_expression(vm,0);
    check(!*reinterpret_cast<std::uint8_t*>(output),"A matching predicate for another entity cannot receive this bail");
    fixture::put(row+8,s.entity); fixture::put<std::uint8_t>(s.entity+0x7e0,1);
    apply_manual_bail_expression(vm,0);
    check(!*reinterpret_cast<std::uint8_t*>(output),"A teleport invalidates gameplay delivery ownership");
    fixture::put<std::uint8_t>(s.entity+0x7e0,0);
    apply_manual_bail_expression(vm,0);
    check(*reinterpret_cast<std::uint8_t*>(output)==1 && *reinterpret_cast<float*>(output+4)==-1.f &&
        !protection().manual_bail.ticket() && protection().momentum.delivered,
        "The authored local predicate receives one manual wipeout and its default weight");
    fixture::put<std::uint8_t>(output,0); fixture::put(output+4,.3f);
    apply_manual_bail_expression(vm,0); publish_animation(s.core);
    check(!*reinterpret_cast<std::uint8_t*>(output) && !fixture::animation_request,
        "Consumed gameplay delivery cannot repeat or continue raising synthetic physics requests");
    cancel_manual_bail();
    ManualBailRequest<unsigned> lifecycle;
    check(lifecycle.queue(11,100) && !lifecycle.begin_selection(22,110,100,true),"Another owner cannot consume a pending bail");
    const auto old=lifecycle.begin_selection(11,110,100,true);
    check(old && lifecycle.pending(*old,599) && !lifecycle.pending(*old,600),
        "The longer momentum lease cannot extend gameplay request delivery beyond its original deadline");
    check(old && lifecycle.selected(*old,300,true),"A selected native bail proceeds to animation");
    check(!lifecycle.begin_publication(11,600,true) && !lifecycle.ticket(),"Stalled publication expires without native writes");
    check(lifecycle.queue(11,700),"A new request can follow expiry");
    lifecycle.cancel(*old);
    check(lifecycle.ticket().has_value(),"An old callback cannot cancel a newer request on the same owner");
    check(lifecycle.queue(22,1200),"An expired pending request cannot permanently block later bails");
    const auto delivery=lifecycle.begin_selection(22,1201,504,true);
    check(delivery && lifecycle.selected(*delivery,504,true) && lifecycle.begin_publication(22,1202,true) &&
        lifecycle.published(*delivery) && lifecycle.begin_publication(22,1203,true) && !lifecycle.published(*delivery),
        "Repeated native publication retains one ticket and reports only the first delivery");
    check(!lifecycle.response(22,1700,true) && !lifecycle.ticket(),
        "Unacknowledged animation delivery expires after 500ms without extending its deadline");
    KeyboardPressLatch key;
    check(!key.update(0x77,true,false) && !key.update(0x77,false,false) && key.update(0x77,true,false) &&
        !key.update(0x77,true,false),"A held key needs release and triggers only once");
    check(!key.update(0x77,true,true) && !key.update(0x77,true,false) && !key.update(0x77,false,false) &&
        key.update(0x77,true,false),"Closing a menu with the bail key held cannot cause a surprise bail");
    ControllerComboLatch pad;
    check(!pad.update(0x300,{true,0,1},true) && !pad.update(0x300,{true,0x300,1},false) &&
        !pad.update(0x300,{true,0,1},false) && pad.update(0x300,{true,0x300,1},false) &&
        !pad.update(0x300,{true,0x300,1},false),"Controller chords require neutral input after inhibition and do not repeat");
    check(overlapping_combos(0x300,0x100) && overlapping_combos(0x100,0x300) && !overlapping_combos(0x300,0x1000),
        "Nested controller chords conflict but unrelated combos remain usable");
    const BailControls controls{true,0x78,0x300};
    check(decode_bail_controls(encode_bail_controls(controls))==controls,"Custom keyboard and controller bindings survive a saved-state roundtrip");
    check(!decode_bail_controls("{\"version\":1,\"enabled\":true,\"key\":-1,\"controller\":0}") &&
        !decode_bail_controls("{\"version\":1,\"enabled\":true,\"key\":119,\"controller\":1024}") &&
        !valid_bail_controls({true,16,0}),"Corrupt saved bindings and modifier-only bails are rejected");
    if (fixture::failures) return 1;
    std::cout<<"Native manual bail bridge, ownership, input and persistence checks passed.\n";
}
