#pragma once
#include <cstdint>

namespace dingosdk {
// Read-only local ownership for features sharing the existing bail hooks.
// Resolves the player binding afresh, including teleports and rig back-links.
struct LocalBailOwner {
    std::uintptr_t base{}, entity{}, world{}, core{}, context{}, rig{}, selector{};
    bool operator==(const LocalBailOwner&) const = default;
};
bool resolve_local_bail_owner(std::uintptr_t client, std::uintptr_t entity, LocalBailOwner& result) noexcept;
// Walking recovery is a verified ground/slide substate of the owned Offboard
// parent, with no outstanding native wipeout animation request.
bool local_bail_recovered(const LocalBailOwner& owner) noexcept;
// Queue from the offline client tick; hooks revalidate the same owner at use.
// Authored gameplay consumes the owned request; native physics/animation follow
// its transition. The bounded fallback supplies scoped physics request bits.
bool queue_manual_bail(std::uintptr_t client,std::uintptr_t entity) noexcept;
void cancel_manual_bail() noexcept;
// Shared native expression runner calls this after the authored predicate.
// Only a queued manual bail for the predicate's verified local ContextKey
// can supply its result; unrelated scripts and entities retain native results.
void apply_manual_bail_expression(std::uintptr_t vm,std::uint32_t pc) noexcept;
struct ManualBailStatus { std::uint64_t queued{},selected{},published{}; };
ManualBailStatus manual_bail_status() noexcept;
bool start_no_bail(std::uintptr_t image_base) noexcept;
bool no_bail_available() noexcept;
// Publish from the validated local client tick. Returns owner availability even
// when both controls are off. Manual protection expires if ticks stop arriving.
bool update_no_bail(std::uintptr_t client, std::uintptr_t entity, bool manual,
    bool flying, std::uint64_t flight_expires) noexcept;
void clear_no_bail() noexcept;
// S.K.A.T.E.: while `locked`, the local skater cannot get back on the board once it is off
// (its mount request is dropped). Publish from the client tick; it expires if ticks stop.
// Releasing it also leaves the skater's teleport option on the board again, since a turn's
// teleport may have set it off (skater component +0xc0) and the SDK's own teleports keep it.
void update_board_lock(std::uintptr_t client, std::uintptr_t entity, bool locked) noexcept;
// The physics state the local skater's selector last chose, for the trainer (air time, bail
// markers). Publish the skater to watch from the client tick; it expires if ticks stop.
struct PhysicsStateWatch {
    bool valid{};
    std::uint32_t state{};
    std::uint32_t previous{}; // the state before this one
    float previous_seconds{}; // how long that one lasted
    std::uint64_t changes{}, wipeouts{}; // counted since the process started
};
void watch_physics_state(std::uintptr_t client, std::uintptr_t entity) noexcept;
PhysicsStateWatch watched_physics_state() noexcept;
// Stopping flight must not discard the independent manual preference.
void clear_no_bail_flight() noexcept;
}
