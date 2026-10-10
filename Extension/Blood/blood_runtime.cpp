#include "blood_runtime.h"
#include "blood_native.h"
#include "Extension/Skater/no_bail.h"
#include "Extension/Profile/local_profile_runtime.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/20260929/blood_contacts.h"
#include "Engine/Game/Build/20260929/replay_activity.h"
#include "Engine/Game/Build/20260929/client_source_spawn.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <memory>

namespace dingosdk::blood {
namespace {
namespace layout=game::build::v20260929::blood_contacts;
struct Watch {LocalBailOwner owner; std::uintptr_t client{}; std::uint64_t until{};};
struct PhysicsJoint {Vec3 position; Region region{}; int parent{};};
struct SampleStatus {std::string availability; std::uint32_t physics_state{}; unsigned contacts{};};
struct State {
    std::mutex mutex;
    std::atomic<std::uintptr_t> rig{},selector{};
    std::atomic<std::uint64_t> level_generation{1};
    std::atomic<bool> suspended{};
    Watch watch;
    BloodModel blood;
    ImpactTracker impacts;
    Snapshot published;
    bool contracts_checked{},contracts_ok{},bail_latched{};
    std::uint64_t last_at{},save_at{};
    std::string map;
};
State& state() {static auto* s=new State; return *s;}
template<class T> bool read(std::uintptr_t address, T& value) { return memory::peek(address, value); }
std::uintptr_t pointer(std::uintptr_t address) {
    std::uintptr_t p{};
    return read(address, p) && p >= 0x10000 && p <= memory::highest_user_address - 0x10000 ? p : 0;
}
bool replay_mode(std::uintptr_t base) {
    namespace r=game::build::v20260929::replay_activity;
    std::uint8_t active{};
    return !read(base+r::playing,active) || active || pointer(pointer(pointer(base+r::client)+r::manager)+r::lease)!=0;
}
bool finite(const Vec3& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x) && std::abs(x) < 1000000; });
}
float length(const Vec3& v) { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
bool read_actor_world(const LocalBailOwner& owner,std::array<float,16>& actor) {
    const auto collection=pointer(owner.entity+0x70);
    std::uint8_t first{},extra{},count{};
    if (!collection || pointer(collection)!=owner.entity ||
        !read(collection+8,count) || count>128 || !read(collection+9,first) || first>128 ||
        !read(collection+10,extra) || extra>32 ||
        !read(collection+0x10+(static_cast<std::uintptr_t>(first)+2*extra)*0x20,actor)) return false;
    for (const auto offset : {0u,4u,8u}) {
        const Vec3 axis{actor[offset],actor[offset+1],actor[offset+2]};
        if (!finite(axis) || length(axis)<.5f || length(axis)>1.5f) return false;
    }
    return finite({actor[12],actor[13],actor[14]});
}
bool contracts(std::uintptr_t base) {
    if (!no_bail_available()) return false;
    for (const auto& contract : {layout::contacts, layout::contacts_before_skeleton, layout::contact_record_reset,
        layout::contact_peak, layout::contact_kinds, layout::skeleton, layout::body_transform}) {
        std::array<unsigned char, 32> actual{};
        if (!read(base + contract.rva, actual) || actual != contract.bytes) return false;
    }
    return true;
}
bool capture(const LocalBailOwner& owner, float dt, bool bailed, const PhysicsContacts& evidence,
    Frame& frame, SampleStatus& output) {
    frame = {};
    frame.entity = owner.entity;
    frame.world = owner.world;
    frame.dt = dt;
    frame.bailed = bailed;

    if (!evidence.valid || evidence.rig!=owner.rig || evidence.entity!=owner.entity || evidence.world!=owner.world) {
        output.availability="Waiting for this physics step's contact evidence.";
        return false;
    }
    std::uint32_t parts_count{};
    const auto pose = pointer(owner.rig + layout::pose_output);
    std::array<unsigned char, 0x24> pose_bytes{};
    const auto header = pose && read(pose, pose_bytes) ? decode_physics_pose_header(pose_bytes) : std::nullopt;
    const auto world_matrices = header ? header->model : 0;
    const auto count = header ? header->count : 0;
    const auto physics = pointer(owner.rig + layout::physics);
    const auto parts = pointer(physics + 0x20);
    const auto contacts = pointer(physics + layout::reporter);
    if (!pose || !world_matrices || !physics || !parts || !contacts ||
        pointer(physics) != owner.base + game::build::v20260929::client_source_spawn::rig_physics_vtable ||
        !header || !read(parts, parts_count) || parts_count != 26 ||
        !read(owner.context + 0x1414, output.physics_state)) {
        output.availability = "Waiting for the standard skater physics and skeleton.";
        return false;
    }
    output.contacts = 0;
    // The physics publisher's "world" matrices are skeleton/model space.
    // The live rig+4760 matrix is the inverse actor placement. Compose with
    // the entity's owned transform to obtain game-world points for the HUD
    // and fall/slide metrics (live verified on the supported build).
    std::array<float, 16> actor{};
    if (!read_actor_world(owner,actor)) {
        output.availability = "Skater world transform is unavailable.";
        return false;
    }
    bool hips{};
    std::array<PhysicsJoint,24> physics_joints{};
    for (std::size_t i = 0; i < layout::body_count; ++i) {
        int joint{}, parent{};
        if (!read(owner.rig + layout::body_map + 4*i, joint) || joint < 0 || joint >= static_cast<int>(count) ||
            !read(owner.rig + layout::body_parents + 4*i, parent) || parent < -1 || parent >= static_cast<int>(layout::body_count) || parent == static_cast<int>(i)) {
            output.availability = "Skeleton body mapping is unavailable.";
            return false;
        }
        const auto mapped = region_for_joint(joint);
        if (!mapped) {
            output.availability = "Unsupported physics joint " + std::to_string(joint) + ".";
            return false;
        }
        auto& at = physics_joints[i];
        at.region = *mapped;
        at.parent = parent;
        Vec3 local{};
        if (!read(world_matrices + static_cast<std::uintptr_t>(joint)*0x40 + 0x30, local) || !finite(local)) return false;
        at.position = model_to_world(actor, local);
        if (joint == 7) { frame.center = at.position; hips = true; }
        // Slot zero contains the array header/dummy body. Native contacts and
        // the SDK's noclip paths use physical bodies 1..23.
        if (!i) continue;
        auto& body = frame.bodies[i-1];
        body.region = *mapped;
        body.joint=joint;
        const auto injury_joint=injury_joint_for_body(joint);
        if (!injury_joint) {output.availability="Unsupported injury body mapping."; return false;}
        body.injury_joint=*injury_joint;
        const auto part = parts + i*layout::body_stride;
        if (pointer(part + 0x10) != physics || !read(part + layout::velocity, body.velocity) ||
            !finite(body.velocity) || length(body.velocity) > 300) return false;
        body.contact = evidence.touching[i] != 0;
        body.pose_valid=read(part+layout::body_pose,body.pose);
        if (body.contact) {
            ++output.contacts;
            const auto& detail=evidence.details[i];
            body.normal_valid=detail.has_value();
            // Preserve the raw flag when detail is absent: missing direction
            // must neither invalidate unrelated bodies nor rearm a held hit.
            if (!detail) continue;
            body.normal=detail->normal;
            body.contact_speed=detail->speed;
            body.speed_valid=true;
            body.contact_point=detail->point;
            body.point_valid=detail->point_valid;
            body.hit=detail->hit;
            frame.grounded = frame.grounded || detail->grounded;
        }
    }
    if (!hips) { output.availability = "The skeleton has no mapped hips joint."; return false; }
    // Ground is an animation marker, not a bone. It can remain on the takeoff
    // surface while a ragdoll falls; never use it to bound the skater's shape.
    for (std::size_t i = 1; i < physics_joints.size(); ++i) {
        const auto& joint = physics_joints[i];
        Vec3 delta{};
        for (std::size_t a = 0; a < 3; ++a) delta[a] = joint.position[a] - frame.center[a];
        if (length(delta) > 6) { output.availability = "Skeleton world pose failed its shape check."; return false; }
    }
    // Board-supported ground states have no body-floor contact while riding.
    if (!bailed) frame.grounded = (output.physics_state >= 100 && output.physics_state < 200) ||
        (output.physics_state >= 400 && output.physics_state < 500);
    frame.body_count = layout::body_count - 1;
    frame.valid = true;
    return true;
}

void reset(State& s) {
    s.watch={}; s.rig=0; s.selector=0; s.bail_latched=false;
    s.last_at=0; s.blood.clear(); s.impacts.reset();
}
}
Snapshot snapshot() {auto& s=state(); std::lock_guard lock(s.mutex); return s.published;}
bool set_options(const Options& options) noexcept {
    if (!valid_options(options)) return false;
    try {
        auto& s=state(); std::lock_guard lock(s.mutex);
        if (!s.published.ready) return false;
        s.published.options=options; s.save_at=GetTickCount64()+350;
        s.published.save_status="Saving blood settings...";
        return true;
    } catch (...) {return false;}
}
void tick(std::uintptr_t base,std::uintptr_t client,std::uintptr_t entity,bool ready,bool noclip,bool editor,std::string_view map) noexcept {
    try {
        auto& s=state();
        LocalBailOwner owner;
        const bool owned=ready && resolve_local_bail_owner(client,entity,owner);
        const bool replaying=replay_mode(base);
        std::unique_lock lock(s.mutex);
        if (!s.contracts_checked && no_bail_available()) {s.contracts_ok=contracts(base); s.contracts_checked=true;}
        constexpr std::string_view key="Blood.Options.v1";
        if (owned && !s.published.ready) {
            if (const auto saved=profile_runtime::local_value(key); saved && saved->is_string()) {
                if (const auto options=decode_options(saved->string())) s.published.options=*options;
                else s.published.save_status="Saved blood settings could not be read; using defaults.";
            }
            s.published.ready=true;
        }
        if (ready && s.save_at && GetTickCount64()>=s.save_at) {
            const auto encoded=encode_options(s.published.options);
            profile_runtime::set_local_values({{std::string(key),encoded}});
            const auto saved=profile_runtime::local_value(key);
            const bool ok=saved && saved->is_string() && saved->string()==encoded;
            s.save_at=ok ? 0 : GetTickCount64()+5000;
            s.published.save_status=ok ? "Blood settings saved." : "Blood settings kept for this session; retrying save.";
        }
        const char* issue=!s.contracts_ok ? "Blood telemetry is unavailable for this game build." :
            s.suspended ? "Blood is suspended during loading." : replaying ? "Blood is suspended in the replay editor." :
            noclip ? "Blood is suspended during Noclip." : editor ? "Blood is suspended in the park editor." :
            !owned ? "Waiting for a local skater." : nullptr;
        const auto& options=s.published.options;
        const bool enabled=options.blood && options.blood_strength>0;
        if (issue || !enabled || (s.watch.owner.entity && (s.watch.owner!=owner || s.map!=map))) reset(s);
        s.map=map;
        if (!issue && enabled) {
            s.watch={owner,client,GetTickCount64()+500}; s.rig=owner.rig; s.selector=owner.selector;
        }
        const bool ground_allowed=!issue && enabled;
        const bool particles_allowed=ground_allowed && s.last_at && GetTickCount64()-s.last_at<250;
        if (!particles_allowed && ground_allowed) s.blood.suspend();
        // Native effects belong to the client thread; never hold the model lock across engine calls.
        const auto scene=std::make_unique<BloodScene>(s.blood.scene());
        lock.unlock();
        const bool current=scene->generation==s.level_generation.load() && !s.suspended.load();
        const auto status=update_native_blood(base,*scene,particles_allowed && current,ground_allowed && current);
        lock.lock();
        s.published.available=!issue && status.available;
        s.published.sources=status.active; s.published.marks=status.marks;
        s.published.status=issue ? issue : !enabled ? "Blood effects are off." : std::string(status.detail);
    } catch (...) {stop_native_blood();}
}
void before_level_transition(unsigned next) noexcept {
    auto& s=state();
    if (next==3 || next==14 || next==22 || next==24 || next==25) {
        s.suspended=true; s.level_generation.fetch_add(1);
        try {std::lock_guard lock(s.mutex); reset(s);} catch (...) {}
        stop_native_blood();
    } else if (next==13 || next==21) s.suspended=false;
}
void observe_selection(std::uintptr_t selector,std::uint32_t next) noexcept {
    auto& s=state();
    if (!selector || selector!=s.selector.load()) return;
    try {std::lock_guard lock(s.mutex);
        if (selector==s.watch.owner.selector && GetTickCount64()<s.watch.until && next==300) s.bail_latched=true;
    } catch (...) {}
}
PhysicsContacts capture_contacts(std::uintptr_t rig) noexcept {
    PhysicsContacts result;
    auto& s=state();
    if (!rig || rig!=s.rig.load(std::memory_order_acquire)) return result;
    try {
        Watch watch;
        {std::lock_guard lock(s.mutex); watch=s.watch;}
        LocalBailOwner current;
        if (rig!=watch.owner.rig || GetTickCount64()>=watch.until ||
            replay_mode(watch.owner.base) || !resolve_local_bail_owner(watch.client,watch.owner.entity,current) ||
            current!=watch.owner) return result;
        const auto physics=pointer(rig+layout::physics);
        const auto reporter=pointer(physics+layout::reporter);
        std::array<std::array<unsigned char,layout::normal_stride>,layout::body_count> records{};
        static_assert(layout::body_count==24);
        if (!physics || !reporter ||
            pointer(physics)!=current.base+game::build::v20260929::client_source_spawn::rig_physics_vtable ||
            !read(reporter+layout::contact_flags,result.touching) ||
            !read(reporter+layout::normals,records)) return result;
        for (std::size_t i=1;i<layout::body_count;++i) {
            if (result.touching[i]>1) return result;
            if (result.touching[i]) result.details[i]=decode_contact_detail(records[i]);
        }
        result.rig=rig; result.entity=current.entity; result.world=current.world; result.valid=true;
    } catch (...) { /* Missing contact evidence never interferes with native response. */ }
    return result;
}

void observe_skeleton(std::uintptr_t rig,float seconds,bool wipeout,const PhysicsContacts& contacts) noexcept {
    auto& s=state();
    if (!rig || rig!=s.rig.load() || s.suspended.load()) return;
    try {
        Watch watch;
        {std::lock_guard lock(s.mutex); watch=s.watch;}
        LocalBailOwner current;
        if (replay_mode(watch.owner.base) || rig!=watch.owner.rig || GetTickCount64()>=watch.until ||
            !resolve_local_bail_owner(watch.client,watch.owner.entity,current) || current!=watch.owner) return;
        std::lock_guard lock(s.mutex);
        if (s.watch.owner!=current || GetTickCount64()>=s.watch.until || s.suspended.load()) return;
        if (!std::isfinite(seconds) || seconds<=0 || seconds>.1f) return;
        std::uint32_t selected{};
        if (!read(current.context+0x1414,selected)) return;
        s.bail_latched=s.bail_latched || wipeout;
        if (!wipeout && ((selected>=100 && selected<300) || (selected>=400 && selected<500) || local_bail_recovered(current))) s.bail_latched=false;
        Frame frame; SampleStatus sample;
        capture(current,seconds,s.bail_latched,contacts,frame,sample);
        if (frame.valid) s.last_at=GetTickCount64();
        if (!s.impacts.running() && frame.valid && !frame.bailed) s.impacts.begin(frame);
        else s.impacts.step(frame);
        const auto& options=s.published.options;
        s.blood.step(frame,s.impacts.impact_contacts(),
            {options.blood,false,options.blood_strength,options.blood_min_damage,options.blood_tuning},s.level_generation.load());
    } catch (...) {}
}
}
