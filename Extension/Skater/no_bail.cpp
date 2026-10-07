#include "no_bail.h"
#include "manual_bail_request.h"
#include "Extension/Slam/slam_runtime.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/supported_build.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/no_bail.h"
#include "Engine/Game/Build/20260929/offboard_flight.h"
#include "Engine/Game/Build/20260929/client_source_spawn.h"
#include <atomic>
#include <cmath>
#include <intrin.h>

namespace dingosdk {
namespace {
using namespace addr::no_bail;
// Native cause recorder: RCX=collector, EDX=reason, XMM2=magnitude.
using RecordCause = void (*)(std::uintptr_t, std::int32_t, float);
using ChooseState = std::uint32_t (*)(std::uintptr_t, std::uint32_t);
using SkeletonResponse = void (*)(std::uintptr_t, float, bool);
using PublishAnimation = void (*)(std::uintptr_t);
using TransitionState = void (*)(std::uintptr_t);
using RagdollEnter = void (*)(std::uintptr_t,std::uintptr_t,std::uintptr_t);
using ResetCauses = void (*)(std::uintptr_t);
constexpr std::uintptr_t highest = memory::highest_user_address;
// The hooks re-resolve the local skater's ownership (some 35 fields) on every
// protected physics step: guarded same-process copies, not a system call each.
template<class T> bool read(std::uintptr_t address, T& value) noexcept { return memory::peek(address, value); }
struct LastError {
    DWORD value = GetLastError();
    ~LastError() { SetLastError(value); }
};
std::uintptr_t pointer(std::uintptr_t object, std::uintptr_t offset = 0) noexcept {
    std::uintptr_t value{};
    if (object < 0x10000 || object > highest - offset || !read(object + offset, value) ||
        value < 0x10000 || value > highest - 0x1000100) return 0;
    return value;
}
struct Owner {
    std::uintptr_t client{}, entity{}, player{}, handle{}, component{}, core{}, context{}, selector{}, causes{}, rig{};
    bool operator==(const Owner&) const = default;
};
struct Lease {
    Owner owner;
    std::uint64_t manual_until{}, flight_until{};
    bool active(std::uint64_t now) const noexcept {
        return now < manual_until || now < flight_until;
    }
};
struct BoardLock {
    Owner owner;
    std::uint64_t until{};
};
struct Protection {
    std::uintptr_t base{};
    bool mount_test{}; // the selector's mount request test is the known one
    BoardLock board;
    RecordCause cause_original{};
    ChooseState choose_original{};
    SkeletonResponse skeleton_original{};
    PublishAnimation publish_original{};
    TransitionState transition_original{};
    RagdollEnter ragdoll_enter_original{};
    ResetCauses reset_causes{};
    std::atomic<bool> ready{};
    SRWLOCK lock = SRWLOCK_INIT;
    Lease lease;
    ManualBailRequest<Owner> manual_bail;
    struct Momentum {
        Owner owner;
        std::array<float,3> velocity{};
        std::uint64_t serial{},until{};
        bool delivered{};
    } momentum;
    std::atomic<std::uint64_t> manual_queued{},manual_selected{},manual_published{};
    std::atomic<bool> expression_pending{};
    std::atomic<std::uint64_t> expression_seen{};
};
Protection& protection() { static auto* value = new Protection; return *value; }
struct StateWatch {
    std::atomic<std::uintptr_t> selector{};
    std::atomic<std::uint64_t> until{}, changes{}, wipeouts{};
    std::atomic<std::uint32_t> state{}, previous{};
    std::atomic<std::int64_t> since{}, previous_ticks{}; // performance-counter ticks
};
StateWatch& state_watch() { static auto* value = new StateWatch; return *value; }

// Recheck live local ownership at use time. A retained physics address alone
// must never protect another skater after a respawn or level change.
bool resolve(std::uintptr_t client, std::uintptr_t entity, Owner& o) noexcept {
    const auto base = protection().base;
    unsigned state{}, manager_offset{};
    if (pointer(client) != base + addr::engine::client_vtable || !read(client + 0xc4, state) ||
        (state != 13 && state != 21) || pointer(entity) != base + addr::engine::skater_entity_vtable ||
        !read(base + addr::engine::context_player_manager_offset, manager_offset) || manager_offset > 0x1000000) return false;
    const auto context = pointer(client, 8);
    const auto manager = pointer(context, manager_offset);
    const auto begin = pointer(manager, 0x4c8), end = pointer(manager, 0x4d0);
    if (!context || pointer(entity, 0x20) != context || pointer(manager) != base + addr::engine::local_player_manager_vtable ||
        !begin || end != begin + 8) return false;
    o.player = pointer(begin);
    std::uint8_t local{}, remote{}, teleport{};
    if (pointer(o.player) != base + addr::engine::local_player_vtable || pointer(o.player, 0x78) != context ||
        !read(o.player + 0x45, local) || local != 1 || !read(o.player + 0x44, remote) || remote ||
        pointer(o.player, 0xb8) != entity || pointer(entity, 0xf8) != o.player ||
        !read(entity + 0x7e0, teleport) || teleport) return false;
    o.handle = pointer(o.player, 0xb0);
    const auto collection = pointer(entity, 0x70);
    o.component = pointer(entity, 0x628);
    o.core = pointer(o.component, 0x70);
    o.context = pointer(o.core, 0x3c0);
    o.selector = pointer(o.core, 0x440);
    o.causes = pointer(o.core, 0x428);
    o.rig = pointer(o.core, 0x438);
    if (pointer(o.handle) != entity + 8 || pointer(collection) != entity ||
        pointer(o.component) != base + addr::engine::skater_component_vtable || pointer(o.component, 0x18) != collection ||
        pointer(o.core) != base + bail_core_vtable || !o.context || !o.selector ||
        pointer(o.selector, 8) != o.context || pointer(o.causes, 0x20) != o.context || pointer(o.rig) != o.context || pointer(o.rig, 0x4630) != o.core)
        return false;
    o.client = client;
    o.entity = entity;
    return true;
}
bool protected_owner(std::uintptr_t object, std::uintptr_t Owner::* member, Owner* owner = nullptr) noexcept {
    auto& p = protection();
    if (!p.ready.load(std::memory_order_acquire)) return false;
    // The lock only copies SDK data, and is never held across native code or
    // memory reads. Hooks do not contend for the debug/camera action lock.
    AcquireSRWLockShared(&p.lock);
    const auto lease = p.lease;
    ReleaseSRWLockShared(&p.lock);
    if (!lease.active(GetTickCount64()) || object != lease.owner.*member) return false;
    Owner current;
    if (!resolve(lease.owner.client, lease.owner.entity, current) || current != lease.owner) return false;
    if (owner) *owner = current;
    return true;
}
bool cancel_request(std::uintptr_t context, std::uintptr_t offset, LONG mask) noexcept {
    const auto address = context + offset;
    if (context < 0x10000 || context > highest - offset - sizeof(LONG) ||
        (address & (alignof(LONG) - 1)) != 0) return false;
    // Consume only the identified request bit. Preserve unrelated native flags
    // even when another producer updates the same word.
    __try {
        _InterlockedAnd(reinterpret_cast<volatile LONG*>(address), ~mask);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool cancel_impact_request(std::uintptr_t context) noexcept {
    return cancel_request(context, impact_request_offset, impact_request_mask);
}
bool cancel_wipeout_requests(std::uintptr_t context) noexcept {
    return cancel_impact_request(context) &&
        cancel_request(context, animation_request_offset, animation_request_mask);
}
bool reset_pending_causes(std::uintptr_t causes) noexcept {
    const auto reset = protection().reset_causes;
    if (!reset) return false;
    __try {
        reset(causes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool filter_requests(std::uintptr_t object, std::uintptr_t Owner::* member) noexcept {
    LastError error;
    Owner owner;
    // Native collision/landing checks also write the collector inline. Reset
    // it at each consumer, after those writes, before either animation's bail
    // or recover/runout query. Filtering record_cause alone misses this path.
    return protected_owner(object, member, &owner) && cancel_wipeout_requests(owner.context) &&
        reset_pending_causes(owner.causes);
}
using ManualTicket=ManualBailRequest<Owner>::Ticket;
void capture_bail_momentum(const ManualTicket& ticket) noexcept {
    auto& p=protection();
    std::array<float,3> velocity{};
    std::uint32_t state{},count{};
    if (!read(ticket.owner.context+0x1414,state)) return;
    if (state!=504) {
        // The rigid board's velocity is in world space. Facing/animation
        // velocity can point the opposite way when riding fakie.
        const auto board=pointer(pointer(ticket.owner.core,0x430),0x18);
        const auto parts=pointer(board,0x20),body=parts+0x130;
        if (pointer(board)!=p.base+addr::client_source_spawn::board_physics_vtable ||
            !read(parts,count) || count!=9 || pointer(body,0x10)!=board ||
            !read(body+0x70,velocity)) return;
    } else if (!read(ticket.owner.context+0x8f0,velocity)) return;
    for (float value:velocity) if (!std::isfinite(value) || std::abs(value)>200) return;
    AcquireSRWLockExclusive(&p.lock);
    const auto pending=p.manual_bail.ticket();
    if (pending && pending->serial==ticket.serial && pending->owner==ticket.owner &&
        p.momentum.serial!=ticket.serial) p.momentum={ticket.owner,velocity,ticket.serial,GetTickCount64()+1500};
    ReleaseSRWLockExclusive(&p.lock);
}
std::optional<ManualTicket> begin_manual_bail(std::uintptr_t object,std::uintptr_t Owner::* member,
    std::uint32_t current,bool publication,bool filtered,bool response=false) noexcept {
    auto& p=protection();
    AcquireSRWLockShared(&p.lock);
    const auto pending=p.manual_bail.ticket();
    ReleaseSRWLockShared(&p.lock);
    if (!pending || object!=pending->owner.*member) return {};
    Owner owner;
    const bool valid=p.ready.load(std::memory_order_acquire) &&
        resolve(pending->owner.client,pending->owner.entity,owner) && owner==pending->owner;
    const bool allowed=valid && !filtered && !protected_owner(object,member);
    AcquireSRWLockExclusive(&p.lock);
    const auto active=p.manual_bail.ticket();
    std::optional<ManualTicket> result;
    if (active && active->serial==pending->serial) {
        if (!valid) p.manual_bail.cancel(*pending);
        else result=response ? p.manual_bail.response(owner,GetTickCount64(),allowed) :
            publication ? p.manual_bail.begin_publication(owner,GetTickCount64(),allowed) :
            p.manual_bail.begin_selection(owner,GetTickCount64(),current,allowed);
        if (!p.manual_bail.ticket()) p.expression_pending.store(false,std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&p.lock);
    if (result && !publication && !response) capture_bail_momentum(*result);
    return result;
}
bool raise_manual_request(std::uintptr_t context,std::uintptr_t offset,LONG mask,bool& owned) noexcept {
    const auto address=context+offset;
    if (context<0x10000 || context>highest-offset-sizeof(LONG) ||
        (address&(alignof(LONG)-1))) return false;
    __try {
        const auto previous=_InterlockedOr(reinterpret_cast<volatile LONG*>(address),mask);
        owned=(previous&mask)==0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
struct ManualRequestScope {
    std::optional<ManualTicket> ticket;
    bool installed{},owned{},wipeout_owned{};
    explicit ManualRequestScope(std::optional<ManualTicket> value) noexcept :ticket(value) {
        Owner current;
        if (ticket && resolve(ticket->owner.client,ticket->owner.entity,current) && current==ticket->owner &&
            !protected_owner(current.selector,&Owner::selector)) {
            installed=raise_manual_request(current.context,impact_request_offset,impact_request_mask,owned) &&
                raise_manual_request(current.context,animation_request_offset,animation_request_mask,wipeout_owned);
        }
    }
    ~ManualRequestScope() {
        if ((!owned && !wipeout_owned) || !ticket) return;
        Owner current;
        // Never clear an existing game's request or write a retired scene.
        if (resolve(ticket->owner.client,ticket->owner.entity,current) && current==ticket->owner) {
            if (owned) (void)cancel_impact_request(current.context);
            if (wipeout_owned) (void)cancel_request(current.context,animation_request_offset,animation_request_mask);
        }
    }
};
// Selection is nested inside transition on the same native worker. Another
// worker or skater cannot borrow this scope or its serial.
thread_local ManualRequestScope* transition_request{};
struct TransitionRequestScope {
    ManualRequestScope* previous=transition_request;
    explicit TransitionRequestScope(ManualRequestScope& request) {transition_request=&request;}
    ~TransitionRequestScope() {transition_request=previous;}
};
void transition_state(std::uintptr_t core) {
    std::uint32_t current{};
    const auto context=pointer(core,0x3c0);
    std::optional<ManualTicket> ticket;
    if (context && read(context+0x1414,current))
        ticket=begin_manual_bail(core,&Owner::core,current,false,false);
    ManualRequestScope request(ticket);
    TransitionRequestScope nested(request);
    protection().transition_original(core);
}
bool momentum_writable(std::uintptr_t address,std::size_t size) noexcept {
    MEMORY_BASIC_INFORMATION region{};
    if (address<0x10000 || address>highest-size ||
        !VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region)) || region.State!=MEM_COMMIT ||
        (region.Protect&PAGE_GUARD)) return false;
    const auto protection=region.Protect&0xff;
    const auto begin=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    return (protection==PAGE_READWRITE || protection==PAGE_WRITECOPY ||
        protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY) &&
        address>=begin && address-begin<=region.RegionSize && size<=region.RegionSize-(address-begin);
}
bool write_bail_expression(std::uintptr_t condition,std::uintptr_t weight) noexcept {
    __try {
        // -1 selects the tree's default branch weighting.
        *reinterpret_cast<float*>(weight)=-1.f;
        *reinterpret_cast<std::uint8_t*>(condition)=1;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool handoff_bail_momentum(const Protection::Momentum& momentum,std::uintptr_t child,std::uintptr_t parent) noexcept {
    Owner current;
    const auto base=protection().base;
    const auto& owner=momentum.owner;
    std::uint32_t state{},count{};
    if (GetTickCount64()>=momentum.until || !resolve(owner.client,owner.entity,current) || current!=owner ||
        protected_owner(owner.selector,&Owner::selector) || !read(owner.context+0x1414,state) || state!=504 ||
        parent!=pointer(owner.core,0x3a8) || parent!=pointer(owner.core,0x3b0) ||
        pointer(parent)!=base+addr::offboard_flight::offboard_flight_vtable ||
        pointer(parent,8)!=owner.context || pointer(parent,0x18)!=owner.rig ||
        pointer(parent,0x10)!=pointer(owner.core,0x430) || pointer(parent,0x48)!=child ||
        child!=parent+ragdoll_child_offset || pointer(child)!=base+ragdoll_child_vtable) return false;
    const auto physics=pointer(owner.rig,0x2f10),parts=pointer(physics,0x20);
    if (pointer(physics)!=base+addr::client_source_spawn::rig_physics_vtable ||
        !read(parts,count) || count!=26) return false;
    std::array<float,3> trajectory{},hips{};
    if (!momentum_writable(child+0x10,sizeof(trajectory)) ||
        !read(child+0x10,trajectory) || !read(parts+23*0x130+0x70,hips)) return false;
    for (float value:momentum.velocity) if (!std::isfinite(value) || std::abs(value)>200) return false;
    for (float value:trajectory) if (!std::isfinite(value) || std::abs(value)>200) return false;
    for (float value:hips) if (!std::isfinite(value) || std::abs(value)>200) return false;
    // Restore translation without erasing relative limb motion or changing
    // gravity, vertical velocity, angular velocity, W, contacts or transforms.
    const std::array<float,3> delta{momentum.velocity[0]-hips[0],0,momentum.velocity[2]-hips[2]};
    std::array<std::array<float,3>,23> velocities{};
    for (std::size_t i=0;i<velocities.size();++i) {
        const auto body=parts+(i+1)*0x130;
        if (pointer(body,0x10)!=physics || !momentum_writable(body+0x60,0x20) ||
            !read(body+0x70,velocities[i])) return false;
        for (std::size_t axis=0;axis<3;++axis) {
            velocities[i][axis]+=delta[axis];
            if (!std::isfinite(velocities[i][axis]) || std::abs(velocities[i][axis])>400) return false;
        }
    }
    trajectory[0]=momentum.velocity[0]; trajectory[2]=momentum.velocity[2];
    __try {
        std::memcpy(reinterpret_cast<void*>(child+0x10),trajectory.data(),sizeof(trajectory));
        for (std::size_t i=0;i<velocities.size();++i) {
            const auto body=parts+(i+1)*0x130;
            std::memcpy(reinterpret_cast<void*>(body+0x70),velocities[i].data(),sizeof(velocities[i]));
            _InterlockedOr(reinterpret_cast<volatile LONG*>(body+0x60),8);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void ragdoll_enter(std::uintptr_t child,std::uintptr_t parent,std::uintptr_t previous) {
    auto& p=protection();
    p.ragdoll_enter_original(child,parent,previous);
    Protection::Momentum momentum;
    std::optional<ManualTicket> ticket;
    // Request delivery has its own 500ms expiry. The native animation may
    // enter ragdoll after that; retain only the captured velocity, not flags.
    AcquireSRWLockShared(&p.lock);
    const auto captured=p.momentum;
    ReleaseSRWLockShared(&p.lock);
    if (!captured.serial || !captured.delivered || GetTickCount64()>=captured.until ||
        parent!=pointer(captured.owner.core,0x3a8) || child!=parent+ragdoll_child_offset) return;
    AcquireSRWLockExclusive(&p.lock);
    if (p.momentum.serial==captured.serial && p.momentum.owner==captured.owner) {
        ticket=ManualTicket{captured.owner,captured.serial};
        momentum=p.momentum; p.momentum={}; p.manual_bail.cancel(*ticket);
        p.expression_pending.store(false,std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&p.lock);
    if (!ticket) return;
    const bool carried=handoff_bail_momentum(momentum,child,parent);
    Owner owner;
    if (resolve(ticket->owner.client,ticket->owner.entity,owner) && owner==ticket->owner)
        slam::observe_manual_bail(owner.selector);
    logging::log(logging::Level::info,logging::Channel::skater,
        "Manual Slam bail entered native ragdoll: forward momentum {} ({}, {}).",
        carried ? "handed over" : "unavailable",momentum.velocity[0],momentum.velocity[2]);
}
bool suppress_cause(std::uintptr_t causes, std::int32_t reason, std::uintptr_t caller) noexcept {
    LastError error;
    Owner owner;
    if (!protected_owner(causes, &Owner::causes, &owner)) return false;
    for (const auto& impact : impact_bail_calls) {
        if (caller == protection().base + impact.return_rva && reason == impact.reason)
            return cancel_impact_request(owner.context);
    }
    return true;
}
void record_cause(std::uintptr_t causes, std::int32_t reason, float magnitude) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const bool protect = suppress_cause(causes, reason, caller);
    // Do not force the native recovery/stumble predicate (recovery_predicate). Its
    // result is also exported to animation at +0x9e, even without a collision.
    // Stop new causes before they reach either the wipeout or runout decision;
    // the native per-step reset still owns clearing the collector's history.
    if (!protect) protection().cause_original(causes, reason, magnitude);
}
// The local skater waiting on foot for its S.K.A.T.E. turn: its mount request goes.
void hold_off_board(std::uintptr_t selector, std::uint32_t current) noexcept {
    auto& p = protection();
    if (current != offboard_physics_state || !p.mount_test || !p.ready.load(std::memory_order_acquire)) return;
    AcquireSRWLockShared(&p.lock);
    const auto board = p.board;
    ReleaseSRWLockShared(&p.lock);
    if (GetTickCount64() >= board.until || selector != board.owner.selector) return;
    Owner current_owner;
    if (!resolve(board.owner.client, board.owner.entity, current_owner) || current_owner != board.owner) return;
    (void)cancel_request(current_owner.context, animation_request_offset, mount_request_mask);
}
std::uint32_t choose_state(std::uintptr_t selector, std::uint32_t current) {
    {
        LastError error;
        hold_off_board(selector, current);
    }
    // Clear shared requests before selection so native ground/air/walking
    // transitions can still run. Some contact tests return Wipeout directly;
    // retain the current state only for that result, never ordinary Offboard.
    const bool filtered = filter_requests(selector, &Owner::selector);
    auto& p=protection();
    std::uint32_t next{};
    {
        const auto enclosing=transition_request;
        const bool nested=enclosing && enclosing->ticket && enclosing->ticket->owner.selector==selector;
        ManualRequestScope local(nested ? std::nullopt : begin_manual_bail(selector,&Owner::selector,current,false,filtered));
        auto& request=nested ? *enclosing : local;
        next=p.choose_original(selector,current);
        if (request.ticket) {
            AcquireSRWLockExclusive(&p.lock);
            const bool selected=p.manual_bail.selected(*request.ticket,next,request.installed && !filtered);
            ReleaseSRWLockExclusive(&p.lock);
            if (selected) {
                ++p.manual_selected;
                logging::log(logging::Level::info,logging::Channel::skater,"Manual Slam bail selected: {} -> {}.",current,next);
            }
        }
    }
    LastError error;
    const auto chosen = filtered && next == wipeout_physics_state && protected_owner(selector, &Owner::selector) ? current : next;
    slam::observe_selection(selector, chosen);
    auto& w = state_watch();
    if (selector == w.selector.load(std::memory_order_acquire) && GetTickCount64() < w.until.load(std::memory_order_acquire)) {
        if (const auto before = w.state.exchange(chosen, std::memory_order_acq_rel); before != chosen) {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            w.previous.store(before, std::memory_order_relaxed);
            w.previous_ticks.store(now.QuadPart - w.since.exchange(now.QuadPart, std::memory_order_relaxed), std::memory_order_relaxed);
            w.changes.fetch_add(1, std::memory_order_relaxed);
            if (chosen == wipeout_physics_state) w.wipeouts.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return chosen;
}
void skeleton_response(std::uintptr_t rig, float seconds, bool wipeout) {
    // The state post-update can raise another request after the selector ran.
    // Filter at this consumer, then let native constraints and recovery run.
    const bool filtered=filter_requests(rig, &Owner::rig);
    if (filtered) wipeout = false;
    const bool observed_wipeout=wipeout;
    // A native impact keeps this request visible during post-physics response.
    // Without it, response applies the normal animation/root correction before
    // the queued bail reaches animation, counteracting the fall's velocity.
    ManualRequestScope request(begin_manual_bail(rig,&Owner::rig,0,false,filtered,true));
    if (request.installed) wipeout=true; // same bool supplied by native caller's bit-27 test
    protection().skeleton_original(rig, seconds, wipeout);
    LastError error;
    slam::observe_skeleton(rig, seconds, observed_wipeout);
}
bool clear_contact_output(std::uintptr_t contacts) noexcept {
    if (contacts < 0x10000 || contacts > highest - body_contact_output_offset) return false;
    __try {
        _InterlockedExchange8(reinterpret_cast<volatile char*>(contacts + body_contact_output_offset), 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void filter_contact_animation(std::uintptr_t core) noexcept {
    LastError error;
    Owner owner;
    std::uint32_t state{};
    if (!protected_owner(core, &Owner::core, &owner) || !read(owner.context + 0x1414, state)) return;
    // Ground/air and grind state families. Offboard, mounting, handplants and
    // existing ragdolls retain their native contact reporting and recovery.
    if (!((state >= 100 && state < 300) || (state >= 400 && state < 500))) return;
    const auto contacts = pointer(pointer(core, 0x3b8), 0x30);
    std::uint8_t contact{};
    if (!contacts || !read(contacts + body_contact_output_offset, contact) || contact > 1) return;
    // Animation gets this sensitive-body hit independently of the ordinary
    // wipeout output and cause collector. Filter it after native publication,
    // before the animation contact context copies it. Keep the general contact
    // latch, collision timers, per-bone records, impulses and physics state.
    if (clear_contact_output(contacts))
        (void)cancel_request(owner.context, body_contact_context_offset, body_contact_context_mask);
}
void publish_animation(std::uintptr_t core) {
    // Animation also reads requests without consulting the cause collector.
    // Clear flags and pending causes before native publication, including
    // its early recovery query and cause export. Leave the native query result
    // unchanged: forcing it true used to put the skater into a stumbling state.
    const bool filtered=filter_requests(core, &Owner::core);
    auto& p=protection();
    {
        ManualRequestScope request(begin_manual_bail(core,&Owner::core,0,true,filtered));
        p.publish_original(core);
        if (request.ticket) {
            AcquireSRWLockExclusive(&p.lock);
            const bool published=p.manual_bail.published(*request.ticket);
            if (request.installed && p.momentum.serial==request.ticket->serial &&
                p.momentum.owner==request.ticket->owner) p.momentum.delivered=true;
            ReleaseSRWLockExclusive(&p.lock);
            if (published && request.installed) {
                ++p.manual_published;
                logging::write(logging::Level::info,logging::Channel::skater,"Manual Slam bail animation published.");
            }
        }
    }
    filter_contact_animation(core);
}
bool compatible(std::uintptr_t base) noexcept {
    if (base < 0x10000 || base > highest - supported_build::game_image_size) return false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!read(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !read(base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.SizeOfImage != supported_build::game_image_size)
        return false;
    std::array<unsigned char, reset_bail_causes_code.size()> reset_code{};
    if (!read(base + reset_bail_causes_rva, reset_code) || reset_code != reset_bail_causes_code) return false;
    for (const auto& contract : {record_bail_cause_contract, choose_physics_state_contract,
            bail_animation_caller_contract, bail_state_caller_contract, bail_transition_contract,bail_ragdoll_enter_contract,
            bail_context_lookup_contract,
            bail_skeleton_contract, bail_publish_contract}) {
        std::array<unsigned char, 32> actual{};
        if (!read(base + contract.rva, actual) || actual != contract.bytes) return false;
    }
    for (const auto& contract : bail_consumer_contracts) {
        std::array<unsigned char, 32> actual{};
        if (!read(base + contract.rva, actual) || actual != contract.bytes) return false;
    }
    for (const auto& contract : body_contact_contracts) {
        std::array<unsigned char, 32> actual{};
        if (!read(base + contract.rva, actual) || actual != contract.bytes) return false;
    }
    for (const auto& impact : impact_bail_calls) {
        std::array<unsigned char, 32> actual{};
        if (!read(base + impact.caller.rva, actual) || actual != impact.caller.bytes) return false;
    }
    return true;
}
}
bool start_no_bail(std::uintptr_t base) noexcept {
    LastError error;
    try {
        auto& p = protection();
        if (p.ready.load()) return p.base == base;
        if (!compatible(base)) {
            logging::write(logging::Level::warning, logging::Channel::skater,
                "No Bail is unavailable: native bail contract did not match.");
            return false;
        }
        p.base = base;
        p.reset_causes = reinterpret_cast<ResetCauses>(base + reset_bail_causes_rva);
        std::array<unsigned char, 32> mount_test{};
        p.mount_test = read(base + mount_request_test_contract.rva, mount_test) &&
                       mount_test == mount_request_test_contract.bytes;
        const std::array targets{
            reinterpret_cast<void*>(base + record_bail_cause_contract.rva),
            reinterpret_cast<void*>(base + choose_physics_state_contract.rva),
            reinterpret_cast<void*>(base + bail_skeleton_contract.rva),
            reinterpret_cast<void*>(base + bail_publish_contract.rva),
            reinterpret_cast<void*>(base + bail_transition_contract.rva),
            reinterpret_cast<void*>(base + bail_ragdoll_enter_contract.rva)};
        const std::array replacements{
            reinterpret_cast<void*>(&record_cause), reinterpret_cast<void*>(&choose_state),
            reinterpret_cast<void*>(&skeleton_response), reinterpret_cast<void*>(&publish_animation),
            reinterpret_cast<void*>(&transition_state),reinterpret_cast<void*>(&ragdoll_enter)};
        std::array<void*, 6> originals{};
        auto status = HookOk;
        std::size_t prepared{};
        for (; prepared < targets.size(); ++prepared) {
            status = hook_prepare(targets[prepared], replacements[prepared], &originals[prepared]);
            if (status != HookOk) break;
            if (!originals[prepared]) { ++prepared; status = HookUnsupportedFunction; break; }
        }
        if (status == HookOk) {
            // Publish every relay before enabling any target.
            p.cause_original = reinterpret_cast<RecordCause>(originals[0]);
            p.choose_original = reinterpret_cast<ChooseState>(originals[1]);
            p.skeleton_original = reinterpret_cast<SkeletonResponse>(originals[2]);
            p.publish_original = reinterpret_cast<PublishAnimation>(originals[3]);
            p.transition_original = reinterpret_cast<TransitionState>(originals[4]);
            p.ragdoll_enter_original = reinterpret_cast<RagdollEnter>(originals[5]);
            for (auto target : targets) {
                status = hook_enable(target);
                if (status != HookOk) break;
            }
            if (status == HookOk) {
                p.ready.store(true, std::memory_order_release);
                logging::write(logging::Level::info, logging::Channel::skater,
                    "No Bail ready: collision and landing causes filtered before physics/animation consume them; noclip is protected.");
                return true;
            }
        }
        // Published relays remain callable even if Detours reports an uncertain
        // attach result. With ready=false any remaining hook simply forwards.
        logging::log(logging::Level::warning, logging::Channel::skater,
            "No Bail hook setup failed (status {}); protected noclip is unavailable.", static_cast<LONG>(status));
        while (prepared) (void)hook_remove(targets[--prepared]);
    } catch (...) {}
    return false;
}
bool no_bail_available() noexcept { return protection().ready.load(std::memory_order_acquire); }
bool queue_manual_bail(std::uintptr_t client,std::uintptr_t entity) noexcept {
    LastError error;
    auto& p=protection();
    Owner owner;
    std::uint32_t current{};
    if (!p.ready.load(std::memory_order_acquire) || !resolve(client,entity,owner) ||
        !read(owner.context+0x1414,current) || !manual_bail_state(current) ||
        protected_owner(owner.selector,&Owner::selector)) return false;
    AcquireSRWLockExclusive(&p.lock);
    const bool queued=p.manual_bail.queue(owner,GetTickCount64());
    if (queued) p.momentum={};
    ReleaseSRWLockExclusive(&p.lock);
    if (queued) ++p.manual_queued;
    if (queued) {
        AcquireSRWLockShared(&p.lock);
        const auto ticket=p.manual_bail.ticket();
        ReleaseSRWLockShared(&p.lock);
        if (ticket) capture_bail_momentum(*ticket);
        p.expression_pending.store(true,std::memory_order_release);
    }
    return queued;
}
void cancel_manual_bail() noexcept {
    auto& p=protection();
    AcquireSRWLockExclusive(&p.lock); p.manual_bail.cancel(); p.momentum={}; ReleaseSRWLockExclusive(&p.lock);
    p.expression_pending.store(false,std::memory_order_release);
}
void apply_manual_bail_expression(std::uintptr_t vm,std::uint32_t pc) noexcept {
    auto& p=protection();
    if (pc || !p.ready.load(std::memory_order_acquire) ||
        !p.expression_pending.load(std::memory_order_acquire)) return;
    LastError error;
    std::uintptr_t resource{},instance{};
    std::uint32_t key{},mask{},state{};
    std::array<std::uint32_t,10> layout{};
    std::array<unsigned char,48> output_contract{};
    if (!read(vm+0x38,resource) || !read(resource+0x10,key) || key!=request_wipeout_graph_key ||
        !read(resource+0x20,layout) || layout!=request_wipeout_graph_layout ||
        !read(resource+request_wipeout_outputs_offset,output_contract) || output_contract!=request_wipeout_outputs ||
        !read(vm+0x30,instance) || pointer(instance)!=resource) return;
    AcquireSRWLockShared(&p.lock);
    const auto ticket=p.manual_bail.ticket();
    const auto captured=p.momentum;
    ReleaseSRWLockShared(&p.lock);
    if (!ticket || !captured.serial || ticket->serial!=captured.serial ||
        GetTickCount64()>=captured.until) return;
    // Native opcode 0x32 stores the byte ConditionOut through page 1 +16;
    // opcode 0x34 stores float WeightOut through page 1 +8. This is the
    // compiled register mapping, independent of the named parameter order.
    const auto arguments=pointer(instance,0x48),context_key=pointer(arguments);
    const auto condition=pointer(arguments,16),weight=pointer(arguments,8);
    std::uint64_t context{},row_key{};
    const auto registry=pointer(p.base+bail_context_registry);
    const auto table=pointer(registry,0x20);
    std::uintptr_t bound_entity{};
    if (!context_key || !read(context_key,context) || !(context&(1ULL<<62)) ||
        !read(registry+0x10,mask) || mask>0xffffff || !table) return;
    const auto row=table+std::uint64_t{static_cast<std::uint32_t>(context)&mask}*320;
    if (!read(row,row_key) || row_key!=context || !read(row+8,bound_entity)) return;
    if (p.expression_seen.exchange(ticket->serial,std::memory_order_relaxed)!=ticket->serial)
        logging::log(logging::Level::info,logging::Channel::skater,
            "Manual Slam gameplay predicate observed: local ContextKey {}.",bound_entity==ticket->owner.entity ? "matches" : "differs");
    Owner owner;
    if (bound_entity!=ticket->owner.entity || !resolve(ticket->owner.client,ticket->owner.entity,owner) ||
        owner!=ticket->owner || protected_owner(owner.selector,&Owner::selector) ||
        !read(owner.context+0x1414,state) || !manual_bail_state(state) ||
        !momentum_writable(condition,1) || !momentum_writable(weight,sizeof(float))) return;
    std::uint8_t native_condition{};
    float native_weight{};
    if (!read(condition,native_condition) || native_condition>1 ||
        !read(weight,native_weight) || !std::isfinite(native_weight)) return;
    bool accepted{};
    AcquireSRWLockExclusive(&p.lock);
    const auto active=p.manual_bail.ticket();
    if (active && active->serial==ticket->serial && active->owner==ticket->owner &&
        p.manual_bail.pending(*ticket,GetTickCount64())) {
        p.manual_bail.cancel(*ticket);
        accepted=true;
    }
    ReleaseSRWLockExclusive(&p.lock);
    if (!accepted) return;
    const bool written=write_bail_expression(condition,weight);
    AcquireSRWLockExclusive(&p.lock);
    if (written && p.momentum.serial==ticket->serial && p.momentum.owner==ticket->owner)
        p.momentum.delivered=true;
    ReleaseSRWLockExclusive(&p.lock);
    p.expression_pending.store(false,std::memory_order_release);
    logging::write(logging::Level::info,logging::Channel::skater,
        written ? "Manual Slam bail delivered to authored gameplay wipeout." : "Manual Slam gameplay output became unavailable.");
}
ManualBailStatus manual_bail_status() noexcept {
    auto& p=protection();
    return {p.manual_queued.load(),p.manual_selected.load(),p.manual_published.load()};
}
bool resolve_local_bail_owner(std::uintptr_t client, std::uintptr_t entity, LocalBailOwner& result) noexcept {
    LastError error;
    Owner owner;
    if (!protection().ready.load(std::memory_order_acquire) || !resolve(client, entity, owner)) return false;
    result = {protection().base, entity, pointer(client, 8), owner.core, owner.context, owner.rig, owner.selector};
    return result.world != 0;
}
bool local_bail_recovered(const LocalBailOwner& owner) noexcept {
    LastError error;
    const auto base=protection().base;
    std::uint32_t state{},requests{};
    if (owner.base!=base || pointer(owner.core)!=base+bail_core_vtable ||
        pointer(owner.core,0x3c0)!=owner.context || pointer(owner.core,0x438)!=owner.rig ||
        pointer(owner.rig)!=owner.context || pointer(owner.rig,0x4630)!=owner.core ||
        !read(owner.context+0x1414,state) || state!=offboard_physics_state ||
        !read(owner.context+animation_request_offset,requests) || (requests&animation_request_mask)) return false;
    const auto parent=pointer(owner.core,0x3b0);
    if (parent!=pointer(owner.core,0x3a8) ||
        pointer(parent)!=base+addr::offboard_flight::offboard_flight_vtable ||
        pointer(parent,8)!=owner.context || pointer(parent,0x18)!=owner.rig ||
        pointer(parent,0x10)!=pointer(owner.core,0x430)) return false;
    const auto active=pointer(parent,0x48);
    for (const auto& substate:addr::offboard_flight::offboard_flight_states) {
        if (substate.offset==0x5b0) continue; // gravity/air is not recovery
        if (active==parent+substate.offset && pointer(active)==base+substate.vtable_rva) return true;
    }
    return false;
}
bool update_no_bail(std::uintptr_t client, std::uintptr_t entity, bool manual,
    bool flying, std::uint64_t flight_expires) noexcept {
    LastError error;
    auto& p = protection();
    Lease next;
    const bool available = p.ready.load(std::memory_order_acquire) && resolve(client, entity, next.owner);
    if (available) {
        const auto now = GetTickCount64();
        next.manual_until = manual ? now + 500 : 0;
        next.flight_until = flying ? flight_expires : 0;
    }
    AcquireSRWLockExclusive(&p.lock);
    p.lease = available ? next : Lease{};
    ReleaseSRWLockExclusive(&p.lock);
    return available;
}
bool set_teleport_on_board(std::uintptr_t component) noexcept {
    __try {
        *reinterpret_cast<volatile std::uint8_t*>(component + 0xc0) = 1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void update_board_lock(std::uintptr_t client, std::uintptr_t entity, bool locked) noexcept {
    LastError error;
    auto& p = protection();
    if (!p.ready.load(std::memory_order_acquire)) return;
    static bool rearm{}; // client tick only
    if (locked) {
        // The owner does not resolve while a teleport is under way: the last one stands
        // until it expires.
        BoardLock next;
        if (resolve(client, entity, next.owner)) {
            next.until = GetTickCount64() + 500;
            AcquireSRWLockExclusive(&p.lock);
            p.board = next;
            ReleaseSRWLockExclusive(&p.lock);
        }
        rearm = true;
        return;
    }
    AcquireSRWLockExclusive(&p.lock);
    p.board = {};
    ReleaseSRWLockExclusive(&p.lock);
    // Released: teleports that keep the skater's own choice (the SDK's /tp) put it on the
    // board again. Once the skater resolves, outside a teleport.
    Owner owner;
    if (rearm && resolve(client, entity, owner) && set_teleport_on_board(owner.component)) rearm = false;
}
void clear_no_bail() noexcept {
    auto& p = protection();
    AcquireSRWLockExclusive(&p.lock);
    p.lease = {};
    ReleaseSRWLockExclusive(&p.lock);
}
void watch_physics_state(std::uintptr_t client, std::uintptr_t entity) noexcept {
    auto& w = state_watch();
    Owner owner;
    if (!protection().ready.load(std::memory_order_acquire) || !resolve(client, entity, owner)) return;
    if (w.selector.exchange(owner.selector, std::memory_order_acq_rel) != owner.selector)
        w.state.store(0, std::memory_order_release);
    w.until.store(GetTickCount64() + 500, std::memory_order_release);
}
PhysicsStateWatch watched_physics_state() noexcept {
    auto& w = state_watch();
    PhysicsStateWatch result;
    result.valid = protection().ready.load(std::memory_order_acquire) && w.selector.load(std::memory_order_acquire) &&
        GetTickCount64() < w.until.load(std::memory_order_acquire);
    result.state = w.state.load(std::memory_order_acquire);
    result.previous = w.previous.load(std::memory_order_relaxed);
    static const double frequency = [] { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return static_cast<double>(value.QuadPart); }();
    result.previous_seconds = static_cast<float>(static_cast<double>(w.previous_ticks.load(std::memory_order_relaxed)) / frequency);
    result.changes = w.changes.load(std::memory_order_relaxed);
    result.wipeouts = w.wipeouts.load(std::memory_order_relaxed);
    return result;
}
void clear_no_bail_flight() noexcept {
    auto& p = protection();
    AcquireSRWLockExclusive(&p.lock);
    p.lease.flight_until = 0;
    ReleaseSRWLockExclusive(&p.lock);
}
}
