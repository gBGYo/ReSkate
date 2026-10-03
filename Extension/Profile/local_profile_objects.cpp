#include "runtime_internal.h"
#include "Extension/Objects/local_placements_runtime.h"
#include "Extension/Objects/ParkEditor/park_editor_runtime.h"
#include "Extension/World/local_world_layers.h"
#include "Extension/Customization/local_customization_runtime.h"
#include "Extension/Skater/skater_teleport.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Game/Build/20260929/local_placements.h"

namespace dingosdk {
using namespace profile_runtime;
ObjectPersistenceModel local_profile_object_persistence() {
    std::lock_guard lock(local_runtime().native_mutex);
    auto& r = placements_runtime();
    ObjectPersistenceModel model;
    model.available = local_runtime().active.load() && r.store && !r.failed && !r.save_failed;
    model.enabled = r.enabled; model.map = r.map; model.clearing = r.clearing;
    if (const auto it = r.document.maps.find(r.map); it != r.document.maps.end()) model.count = it->second.size();
    model.busy = r.clearing || r.inflight.has_value();
    for (const auto& row : r.rows) model.rows.push_back({row.token, row.object.item, row.object.position, row.saved_id != 0, row.spawned});
    model.can_clear = model.available && world_layers_runtime().model.ready && !r.map.empty() &&
        !model.busy && model.count != 0;
    model.status = r.status;
    if (r.save_failed) model.status = "Object save failed. The previous file is retained; restart after checking objects.sqlite3.";
    if (!r.store || r.failed) model.status = "Object persistence is unavailable. Check objects.sqlite3 and the runtime log.";
    return model;
}
bool set_local_object_persistence(bool enabled) {
    std::lock_guard lock(local_runtime().native_mutex);
    auto& r = placements_runtime();
    if (!local_runtime().active || !r.store || r.failed || r.save_failed || park_editor_owns_placements()) return false;
    try {
        local_runtime().store->set_bool_option("ReSkate.ObjectPersistence", enabled);
        r.enabled = enabled;
        r.status = enabled ? "Object persistence enabled." : "Object persistence disabled. Saved layouts are retained.";
        return true;
    } catch (...) { r.status = "Could not save the object persistence option."; return false; }
}
bool clear_local_persisted_objects(std::string_view map) {
    std::lock_guard lock(local_runtime().native_mutex);
    auto& r = placements_runtime();
    if (!local_runtime().active || !placement_session_ready() || r.map != map) return false;
    std::set<std::uint64_t> tokens;
    for (const auto& row : r.rows) if (row.saved_id) tokens.insert(row.token);
    return begin_placement_delete(tokens);
}
bool delete_local_placed_object(std::string_view map, std::uint64_t token) {
    std::lock_guard lock(local_runtime().native_mutex);
    auto& r = placements_runtime();
    if (!local_runtime().active || !placement_session_ready() || r.map != map ||
        std::none_of(r.rows.begin(), r.rows.end(), [&](const auto& row) { return row.token == token; })) return false;
    return begin_placement_delete({token});
}
bool teleport_local_skater(const std::array<float, 3>& position) {
    std::lock_guard lock(local_runtime().native_mutex);
    auto& r = placements_runtime();
    if (!local_runtime().active || !r.teleport || !r.transition_ctor || !r.transition_destroy) return false;
    for (const auto v : position)
        if (!std::isfinite(v) || std::abs(v) > 1e6f) return false;
    r.position_teleport = position;
    r.position_teleport_at = GetTickCount64();
    return true;
}
SkaterTeleportSubmission submit_owned_skater_teleport(std::uintptr_t client,const LocalBailOwner& owner,
    const std::array<float,16>& transform) noexcept {
    try {
        std::lock_guard lock(local_runtime().native_mutex);
        auto& r=placements_runtime();
        if (!local_runtime().active || local_runtime().base!=owner.base ||
            cosmetic_runtime().update_thread!=GetCurrentThreadId() || !r.teleport || !r.context ||
            !r.transition_ctor || !r.transition_destroy || !valid_skater_teleport_transform(transform)) return {};
        LocalBailOwner current;
        if (!resolve_local_bail_owner(client,owner.entity,current) || current!=owner) return {};
        std::uintptr_t manager{}; std::uint32_t state{},serial{};
        if (!read(owner.base+addr::local_placements::teleport_manager,manager) || !manager ||
            !read(manager,state) || !read(manager+0x7c,serial)) return {};
        if (state!=0 || r.position_teleport || r.teleport_token || !placement_client_channel())
            return {SkaterTeleportState::busy,{}};
        alignas(16) const auto pose=transform;
        alignas(16) std::array<std::uint64_t,2> transition{};
        r.transition_ctor(transition.data());
        struct Destroy { decltype(r.transition_destroy) call; void* value; ~Destroy() { call(value); } } destroy{r.transition_destroy,transition.data()};
        // 0x565010 copies all 64 bytes and increments manager+0x7c. Its
        // decompiled return value is void; observe the counter explicitly.
        using Teleport=void (*)(std::uintptr_t,const float*,unsigned,const void*);
        reinterpret_cast<Teleport>(r.teleport)(manager,pose.data(),1,transition.data());
        std::uint32_t submitted{};
        const auto expected=serial==UINT32_MAX-1 ? 0u : serial+1;
        if (!read(manager+0x7c,submitted) || submitted!=expected) return {};
        logging::log(logging::Level::info,logging::Channel::skater,
            "Slam saved-start teleport submitted: request {}, position {:.3f}, {:.3f}, {:.3f}, forward {:.3f}, {:.3f}, {:.3f}.",
            submitted,pose[12],pose[13],pose[14],pose[8],pose[9],pose[10]);
        return {SkaterTeleportState::submitted,{owner.base,manager,submitted}};
    } catch (...) {return {};}
}
SkaterTeleportState inspect_skater_teleport(const SkaterTeleportReceipt& receipt) noexcept {
    std::uintptr_t manager{}; std::uint32_t state{},serial{};
    if (!receipt.base || !receipt.manager ||
        !memory::peek(receipt.base+addr::local_placements::teleport_manager,manager) || manager!=receipt.manager ||
        !memory::peek(manager,state) || !memory::peek(manager+0x7c,serial)) return SkaterTeleportState::unavailable;
    if (serial!=receipt.serial) return SkaterTeleportState::interrupted;
    return state==0 ? SkaterTeleportState::idle : SkaterTeleportState::busy;
}
bool teleport_to_local_placed_object(std::string_view map, std::uint64_t token) {
    std::lock_guard lock(local_runtime().native_mutex);
    auto& r = placements_runtime();
    if (!local_runtime().active || !placement_session_ready() || r.map != map || r.clearing ||
        std::none_of(r.rows.begin(), r.rows.end(), [&](const auto& row) { return row.token == token; })) return false;
    r.teleport_token = token; r.teleport_queued_at = GetTickCount64();
    r.status = "Teleport queued...";
    return true;
}
}
