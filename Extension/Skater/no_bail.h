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
// Stopping flight must not discard the independent manual preference.
void clear_no_bail_flight() noexcept;
}
