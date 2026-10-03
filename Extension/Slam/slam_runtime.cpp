#include "slam_runtime.h"
#include "slam_telemetry.h"
#include "slam_pose.h"
#include "slam_pose_publication.h"
#include "slam_audio.h"
#include "slam_retry.h"
#include "Extension/Multiplayer/Remote/native_pose_layout.h"
#include "Extension/Multiplayer/Remote/native_skater.h"
#include "Extension/Skater/no_bail.h"
#include "Extension/Skater/manual_bail_request.h"
#include "Extension/Profile/local_profile_runtime.h"
#include "Extension/Settings/named_settings.h"
#include "Extension/UI/Overlay/overlay.h"
#include "Extension/UI/Overlay/input_capture.h"
#include "Engine/Core/Platform/launcher_support.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Game/Build/20260929/slam.h"
#include "Engine/Game/Build/20260929/client_source_spawn.h"
#include "Engine/Game/UI/live_game_view.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <future>

namespace dingosdk::slam {
namespace {
namespace layout = game::build::v20260929::slam;
struct Watch { LocalBailOwner owner; std::uintptr_t client{}, component{}, holder{}; std::uint64_t until{}; };
struct CompletedAttempt { std::uint64_t serial{}; std::string map; Result result; };
struct State {
    std::mutex mutex;
    std::atomic<std::uintptr_t> rig{}, selector{};
    std::atomic<std::uintptr_t> animation_interface{};
    std::atomic<std::uintptr_t> animation_component{};
    std::atomic<bool> visible{};
    std::atomic<bool> draw_active{};
    Watch watch;
    PosePublication pose_publication;
    Challenge challenge;
    SavedStartRetry retry;
    std::atomic<std::uint64_t> level_generation{1};
    FreeplayXray normal_xray;
    Snapshot published;
    Frame last;
    std::optional<Action> pending;
    std::uint64_t last_at{};
    bool bail_latched{}, contracts_checked{}, contracts_ok{};
    bool pose_hooks_checked{}, pose_hooks_ok{};
    std::string map, diagnostic, pose_diagnostic,retry_diagnostic;
    std::future<std::shared_ptr<const SkeletonMesh>> mesh_load;
    bool mesh_started{};
    bool normal_play_observed{};
    bool render_caller_logged{};
    bool render_hook_checked{},render_hook_ok{},render_instance_logged{};
    std::uintptr_t render_object{};
    std::uint32_t render_source_changes{};
    std::shared_ptr<const MeshPose> raster_pose;
    void (*original_render_instance)(std::uintptr_t,std::uintptr_t){};
    bool cache_hook_checked{},cache_hook_ok{},cache_logged{};
    std::uint32_t (*original_render_cache)(std::uintptr_t,std::uintptr_t){};
    bool view_hook_checked{},view_hook_ok{},view_logged{};
    std::atomic<bool> camera_contracts_ok{};
    void (*original_render_view)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uint8_t){};
    std::uint64_t visuals_save_at{};
    std::uint64_t controls_save_at{};
    std::uint64_t explicit_bail_until{};
    std::uint64_t manual_bail_at{};
    KeyboardPressLatch bail_key;
    ControllerComboLatch bail_controller;
    std::uint64_t config_save_at{}, best_retry_at{}, attempt_serial{};
    BestBook best_book;
    std::vector<CompletedAttempt> completed_attempts;
    std::string attempt_map, selected_best_identity;
    SlowMotionPulse slow_pulse;
    ImpactCameraPulse camera_pulse;
    CameraImpulse camera_impact;
    CameraMotion previous_camera_motion;
    std::uintptr_t previous_effect_camera{};
    std::uint64_t camera_logged_impulse{};
    std::atomic<bool> effects_suspended{}, effects_cancel_requested{};
    std::string time_status="Impact slow motion ready.";
};
State& state() { static auto* s = new State; return *s; }
bool game_window_focused() {
    DWORD process{};
    GetWindowThreadProcessId(GetForegroundWindow(),&process);
    return process==GetCurrentProcessId();
}
bool gameplay_focused() {return game_window_focused() && overlay::keyboard_shortcuts_allowed();}
TimePulseFrame update_time_effect(const VisualOptions& options,const VisualEvents& events,bool allowed) {
    auto& s=state();
    auto pulse=s.slow_pulse.step(options,events,GetTickCount64(),allowed);
    if (pulse.started) {
        if (impact_time_scale_active() && !restore_impact_time_scale()) {
            s.slow_pulse.cancel(); s.time_status="Waiting to restore the previous game speed."; return {};
        }
        if (!begin_impact_time_scale(pulse.factor)) {
            s.slow_pulse.cancel(); s.time_status="Impact slow motion is unavailable for the current game settings.";
            logging::write(logging::Level::warning,logging::Channel::skater,s.time_status);
            return {};
        }
        s.time_status="Impact slow motion active.";
        logging::log(logging::Level::info,logging::Channel::skater,
            "Impact slow motion applied: factor {:.2f}, duration {:.2f}s, bone {}, severity {:.1f}, fracture={}." ,
            pulse.factor,options.slow_motion_seconds,events.latest_bone,events.latest_severity,events.latest_fracture);
    } else if (pulse.active && !update_impact_time_scale(pulse.factor)) {
        s.slow_pulse.cancel();
        if (impact_time_scale_active())
            s.time_status=restore_impact_time_scale() ? "Previous game speed restored." : "Waiting to restore the previous game speed.";
        else s.time_status="A newer game-speed setting was preserved.";
        return {};
    } else if (!pulse.active && impact_time_scale_active()) {
        if (restore_impact_time_scale()) {
            s.time_status="Previous game speed restored.";
            logging::write(logging::Level::info,logging::Channel::skater,"Impact slow motion restored the prior game speed.");
        } else s.time_status="Waiting to restore the previous game speed.";
    }
    return pulse;
}
template<class T> bool read(std::uintptr_t address, T& value) { return memory::peek(address, value); }
std::uintptr_t pointer(std::uintptr_t address) {
    std::uintptr_t p{};
    return read(address, p) && p >= 0x10000 && p <= memory::highest_user_address - 0x10000 ? p : 0;
}
bool finite(const Vec3& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x) && std::abs(x) < 1000000; });
}
float length(const Vec3& v) { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
bool read_actor_world(const LocalBailOwner& owner,PoseMatrix& actor) {
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
std::uintptr_t pool_record(std::uintptr_t pool,std::uint32_t index,std::size_t stride) {
    if (index>1000000) return 0;
    const auto n=std::uint64_t{index}+16;
    const auto shift=std::bit_width(n)-1;
    const auto page=pointer(pool+8*(shift-4)+8);
    return page ? page+(n-(std::uint64_t{1}<<shift))*stride : 0;
}
std::uint32_t local_render_index(const Watch& watch,unsigned slot) {
    std::array<std::uintptr_t,3> managers{};
    for (std::size_t i=0;i<managers.size();++i) {
        managers[i]=pointer(watch.owner.base+layout::render_managers[i]);
        if (!managers[i] || pointer(managers[i])!=watch.owner.base+layout::render_manager_vtables[i]) return UINT32_MAX;
    }
    std::uint32_t h1{},h2{},h3{};
    if (!read(watch.owner.entity+0x4d0+8*slot,h1) || h1<2) return UINT32_MAX;
    const auto r1=pool_record(managers[0]+0xc8,h1-2,0x98);
    if (!r1 || !read(r1+0x40,h2) || h2<2) return UINT32_MAX;
    const auto r2=pool_record(managers[1]+0xc8,h2-2,0x68);
    if (!r2 || !read(r2+0x0c,h3) || h3<2) return UINT32_MAX;
    return h3-2;
}
bool refresh_render_pose(const Watch& watch,std::uintptr_t object,const SkeletonMesh& mesh,MeshPose& output) {
    // Read the same fields exported by 48db8e0. Never invoke skinning or
    // modify native state from Present. Dirty/locked buffers are left alone.
    std::uint32_t locked{},count{};
    std::array<std::uint8_t,3> flags{};
    if (!read(object+0x104,locked) || locked || !read(object+0xfe,flags) ||
        flags[0]>1 || flags[1]!=1 || flags[2] || !read(object+0xf8,count) ||
        count<render_bone_count || count>395) return false;
    const auto version=pointer(object+0x28),bones_at=pointer(object+0xe0),root_at=pointer(object+0xd8);
    if (!version || !bones_at) return false;
    PoseMatrix world{};
    std::array<float,4> root{};
    if ((flags[0] && !read(object+0x50,world)) || (root_at && !read(root_at,root))) return false;
    std::array<std::array<float,12>,render_bone_count> bones{},verified{};
    if (!read(bones_at,bones) || !read(bones_at,verified) ||
        std::memcmp(bones.data(),verified.data(),sizeof(bones))!=0) return false;
    std::array<float,4> root_after{}; PoseMatrix world_after{};
    std::array<std::uint8_t,3> flags_after{};
    std::uint32_t count_after{};
    if ((root_at && (!read(root_at,root_after) || std::memcmp(root.data(),root_after.data(),sizeof(root))!=0)) ||
        (flags[0] && (!read(object+0x50,world_after) || std::memcmp(world.data(),world_after.data(),sizeof(world))!=0)) ||
        pointer(object+0x28)!=version || pointer(object+0xe0)!=bones_at || pointer(object+0xd8)!=root_at ||
        !read(object+0xfe,flags_after) || flags_after!=flags || !read(object+0xf8,count_after) || count_after!=count ||
        !read(object+0x104,locked) || locked || pointer(object)!=watch.owner.base+layout::render_instance_vtable) return false;
    auto placement=identity_pose;
    for (std::size_t axis=0;axis<3;++axis) placement[12+axis]=root[axis];
    if (flags[0]) {
        for (const auto lane : {3u,7u,11u}) world[lane]=0;
        world[15]=1;
        placement=compose_matrices(placement,world);
    }
    return place_packed_render_skin(mesh,bones,placement,output);
}
void render_instance_hook(std::uintptr_t object,std::uintptr_t packet) {
    auto& s=state();
    s.original_render_instance(object,packet);
    if (!s.draw_active.load(std::memory_order_acquire)) return;
    struct PreserveError {DWORD value=GetLastError(); ~PreserveError() {SetLastError(value);}} preserve_error;
    try {
        std::uint32_t count{},index{};
        std::uint8_t visible{};
        if (!read(packet+136,count) || count<render_bone_count || count>395 || !read(object+0x108,index)) return;
        // Riding and walking have separate render instances. The hidden one
        // must not replace the visible instance with its older animation.
        if (!read(packet+153,visible) || !visible || index>1000000) return;
        Watch watch; std::shared_ptr<const SkeletonMesh> mesh;
        {std::lock_guard lock(s.mutex); watch=s.watch; mesh=s.published.mesh;}
        if (!mesh || GetTickCount64()>=watch.until) return;
        LocalBailOwner current;
        if (!resolve_local_bail_owner(watch.client,watch.owner.entity,current) || current!=watch.owner) return;
        bool local{};
        for (unsigned slot=0;slot<2;++slot) local=local || index==local_render_index(watch,slot);
        if (!local) return;
        {
            std::lock_guard lock(s.mutex);
            if (!s.render_instance_logged) {
                s.render_instance_logged=true;
                logging::log(logging::Level::info,logging::Channel::graphics,
                    "X-ray native draw packet: {} bones, local index {}, instance {:#x}.",count,index,object);
                // One local-only trace identifies the renderer consuming this
                // packet; it never captures remote or unrelated instances.
                std::array<void*,10> frames{};
                const auto found=CaptureStackBackTrace(1,static_cast<DWORD>(frames.size()),frames.data(),nullptr);
                std::string callers;
                for (unsigned i=0;i<found;++i) {
                    const auto address=reinterpret_cast<std::uintptr_t>(frames[i]);
                    if (address>=current.base && address-current.base<0x9144000)
                        callers+=std::format(" Skate+{:#x}",address-current.base);
                }
                logging::log(logging::Level::info,logging::Channel::graphics,"X-ray draw packet consumer:{}",callers);
            }
        }
        // 48db8e0 performs the final world placement and packing on demand,
        // then returns the exact bone buffer and flags used by the draw.
        const auto bones_at=pointer(packet+128);
        std::array<std::array<float,12>,render_bone_count> bones{};
        if (!bones_at || !read(bones_at,bones)) return;
        // These skin matrices already contain the player's world placement.
        // The instance matrix repeats that placement and must not be applied
        // again. Native physics rendering can extract the trajectory's world
        // translation into a separate header; restore only that translation.
        auto placement=identity_pose;
        const auto root_at=pointer(packet+144);
        if (root_at) {
            std::array<float,4> root{};
            if (!read(root_at,root)) return;
            for (std::size_t axis=0;axis<3;++axis) placement[12+axis]=root[axis];
        }
        std::uint8_t relative{};
        if (!read(packet+152,relative)) return;
        if (relative) {
            PoseMatrix actor{};
            if (!read(packet,actor)) return;
            // Unpack only affine components; native matrix fourth lanes carry
            // metadata. Restore the extracted root before actor placement.
            for (const auto lane : {3u,7u,11u}) actor[lane]=0;
            actor[15]=1;
            placement=compose_matrices(placement,actor);
        }
        auto next=std::make_shared<MeshPose>();
        if (!place_packed_render_skin(*mesh,bones,placement,*next)) return;
        next->at_ms=GetTickCount64(); next->render_export=true; next->native_draw=true;
        std::lock_guard lock(s.mutex);
        if (s.watch.owner!=current || s.watch.holder!=watch.holder || GetTickCount64()>=s.watch.until) return;
        next->sequence=++s.published.rendered_poses;
        s.published.mesh_pose=std::move(next);
        // Bound source diagnostics per process. Inactive riding/walking
        // instances can retain visible pose packets; verify ownership changes
        // instead of treating every accepted getter as the rendered character.
        if (s.render_object!=object && s.render_source_changes<32) {
            ++s.render_source_changes;
            logging::log(logging::Level::info,logging::Channel::graphics,
                "X-ray pose source change {}: {:#x} -> {:#x}, local index {}, slots {}/{}, thread {}.",
                s.render_source_changes,s.render_object,object,index,
                local_render_index(watch,0),local_render_index(watch,1),GetCurrentThreadId());
        }
        s.render_object=object;
    } catch (...) {}
}
bool install_render_hook(std::uintptr_t base) {
    auto& s=state();
    std::array<unsigned char,32> bytes{};
    if (!read(base+layout::render_instance.rva,bytes) || bytes!=layout::render_instance.bytes) return false;
    void* original{};
    auto target=reinterpret_cast<void*>(base+layout::render_instance.rva);
    if (hook_prepare(target,reinterpret_cast<void*>(&render_instance_hook),&original)!=HookOk) return false;
    s.original_render_instance=reinterpret_cast<decltype(s.original_render_instance)>(original);
    if (hook_enable(target)!=HookOk) return false;
    return true;
}
std::uint32_t render_cache_hook(std::uintptr_t object,std::uintptr_t packet) {
    auto& s=state();
    const auto changed=s.original_render_cache(object,packet);
    struct PreserveError {DWORD value=GetLastError(); ~PreserveError() {SetLastError(value);}} preserve_error;
    if (!s.draw_active.load(std::memory_order_acquire)) return changed;
    try {
        Watch watch; std::uintptr_t native{};
        {std::lock_guard lock(s.mutex); if (s.cache_logged) return changed; watch=s.watch; native=s.render_object;}
        if (!native || GetTickCount64()>=watch.until || pointer(object+0x60)!=native) return changed;
        LocalBailOwner current;
        if (!resolve_local_bail_owner(watch.client,watch.owner.entity,current) || current!=watch.owner) return changed;
        const auto begin=pointer(object+0x10),end=pointer(object+0x18);
        if (!begin || !end || end<begin || (end-begin)%8 || end-begin>512) return changed;
        std::string callbacks;
        for (auto at=begin;at<end && at<begin+64;at+=8) {
            const auto child=pointer(at),vtable=pointer(child),method=pointer(vtable+8);
            if (method>=current.base && method-current.base<0x9144000)
                callbacks+=std::format(" [{:#x}:vt+{:#x},update+{:#x}]",child,vtable-current.base,method-current.base);
        }
        std::lock_guard lock(s.mutex);
        if (s.cache_logged || s.watch.owner!=current) return changed;
        s.cache_logged=true;
        logging::log(logging::Level::info,logging::Channel::graphics,
            "X-ray renderer cache: object {:#x}, native {:#x}, bones {:#x}, callbacks:{}",
            object,native,pointer(object+0x110),callbacks);
    } catch (...) {}
    return changed;
}
bool install_cache_hook(std::uintptr_t base) {
    auto& s=state(); std::array<unsigned char,32> bytes{};
    if (!read(base+layout::render_cache.rva,bytes) || bytes!=layout::render_cache.bytes) return false;
    void* original{};
    auto target=reinterpret_cast<void*>(base+layout::render_cache.rva);
    if (hook_prepare(target,reinterpret_cast<void*>(&render_cache_hook),&original)!=HookOk) return false;
    s.original_render_cache=reinterpret_cast<decltype(s.original_render_cache)>(original);
    return hook_enable(target)==HookOk;
}
struct alignas(16) ImpactViews {
    std::array<std::byte,320> current{},previous{};
    std::array<std::byte,320> source_current{};
    RenderCamera camera;
    LocalBailOwner owner;
};
std::optional<ImpactViews> impact_views(std::uintptr_t current_view,std::uintptr_t previous_view) {
    auto& s=state();
    if (!s.camera_contracts_ok.load(std::memory_order_acquire) || !s.draw_active.load(std::memory_order_acquire) || s.effects_suspended.load(std::memory_order_acquire) ||
        !gameplay_focused() || current_view<0x10050 || previous_view<0x10050) return {};
    Watch watch; CameraImpulse impulse; CameraMotion previous;
    {
        std::lock_guard lock(s.mutex);
        watch=s.watch; impulse=s.camera_impact;
        if (s.published.first_person) return {};
    }
    if (GetTickCount64()>=watch.until) return {};
    auto camera=latest_game_view();
    // Freecam and first person own their camera. This response only layers
    // over the ordinary local gameplay view, never over those features.
    if (!camera || !refresh_game_view(watch.owner.base,*camera) ||
        pointer(camera->camera)!=watch.owner.base+game::build::v20260929::engine::camera_vtable) return {};
    std::uint32_t kind{}; std::uint8_t projection{};
    // Live main perspective input uses projection kind 0; kind 3 is a
    // separate projection branch. Preserve all projection-specific fields.
    if (!read(current_view-0x50+0x18,kind) || kind || !read(current_view+0x100,projection) || projection!=0) return {};
    const auto motion=sample_camera_impulse(impulse,GetTickCount64());
    {
        std::lock_guard lock(s.mutex);
        if (s.previous_effect_camera==camera->camera) previous=s.previous_camera_motion;
        if (!motion.active && !previous.active) return {};
    }
    ImpactViews views;
    std::array<std::byte,320> current_check{},previous_check{};
    if (!read(current_view,views.current) || !read(previous_view,views.previous)) return {};
    RenderCamera unmodified,old;
    if (!decode_render_camera(views.current,unmodified) || !decode_render_camera(views.previous,old)) return {};
    const Vec3 delta{unmodified.world[12]-camera->world[12],unmodified.world[13]-camera->world[13],unmodified.world[14]-camera->world[14]};
    if (!finite(delta) || length(delta)>10 || !resolve_local_bail_owner(watch.client,watch.owner.entity,views.owner) || views.owner!=watch.owner ||
        !read(current_view,current_check) || current_check!=views.current || !read(previous_view,previous_check) || previous_check!=views.previous) return {};
    views.source_current=views.current;
    if (!offset_render_camera(views.current,motion.translation,motion.roll,motion.fov_scale,views.camera) ||
        !offset_render_camera(views.previous,previous.translation,previous.roll,previous.fov_scale,old)) return {};
    {
        std::lock_guard lock(s.mutex);
        if (s.watch.owner!=watch.owner || GetTickCount64()>=s.watch.until) return {};
        s.previous_effect_camera=camera->camera; s.previous_camera_motion=motion;
        if (motion.active && s.camera_logged_impulse!=impulse.started_ms) {
            s.camera_logged_impulse=impulse.started_ms;
            logging::log(logging::Level::info,logging::Channel::skater,
                "Impact camera submitted: strength {:.2f}, native FOV {:.2f} -> {:.2f} degrees.",
                impulse.strength,unmodified.vertical_fov,views.camera.vertical_fov);
        }
    }
    return views;
}
void render_view_hook(std::uintptr_t blackboard,std::uintptr_t current_view,
    std::uintptr_t previous_view,std::uint8_t jitter) {
    auto& s=state();
    std::optional<ImpactViews> impact;
    const auto incoming_error=GetLastError();
    try {impact=impact_views(current_view,previous_view);} catch (...) {}
    SetLastError(incoming_error);
    // Native setter copies all 320 input bytes into its blackboard and
    // derives matrices synchronously. These scoped copies cannot persist
    // an override in the game's camera or mutate the caller's native view.
    s.original_render_view(blackboard,impact ? reinterpret_cast<std::uintptr_t>(impact->current.data()) : current_view,
        impact ? reinterpret_cast<std::uintptr_t>(impact->previous.data()) : previous_view,jitter);
    struct PreserveError {DWORD value=GetLastError(); ~PreserveError() {SetLastError(value);}} preserve_error;
    if (!s.draw_active.load(std::memory_order_acquire)) return;
    try {
        Watch watch; std::uintptr_t object{};
        std::shared_ptr<const SkeletonMesh> mesh;
        std::shared_ptr<const MeshPose> producer;
        {
            std::lock_guard lock(s.mutex);
            watch=s.watch; object=s.render_object; mesh=s.published.mesh; producer=s.published.mesh_pose;
        }
        if (GetTickCount64()>=watch.until || current_view<0x10050) return;
        // Only the main native raster view. Reflection, shadow, editor and
        // effect views must not establish the local character camera.
        std::uint32_t kind{};
        std::array<std::byte,320> input{},verified{};
        RenderCamera render_camera;
        if (!read(current_view-0x50+0x18,kind) || kind || !read(current_view,input) ||
            !decode_render_camera(input,render_camera)) return;
        auto camera=latest_game_view();
        if (!camera || !refresh_game_view(watch.owner.base,*camera)) return;
        const auto& world=render_camera.world;
        const Vec3 delta{world[12]-camera->world[12],world[13]-camera->world[13],world[14]-camera->world[14]};
        if (!finite(delta) || length(delta)>10) return;
        LocalBailOwner owner;
        if (!resolve_local_bail_owner(watch.client,watch.owner.entity,owner) || owner!=watch.owner) return;
        if (!object || !mesh || !producer || !producer->native_draw ||
            GetTickCount64()-producer->at_ms>=250 || pointer(object)!=owner.base+layout::render_instance_vtable) return;
        std::uint32_t index{};
        if (!read(object+0x108,index) ||
            (index!=local_render_index(watch,0) && index!=local_render_index(watch,1))) return;
        auto next=std::make_shared<MeshPose>();
        if (!refresh_render_pose(watch,object,*mesh,*next) || !read(current_view,verified) || input!=verified ||
            !read(current_view-0x50+0x18,kind) || kind) return;
        LocalBailOwner after;
        if (!resolve_local_bail_owner(watch.client,watch.owner.entity,after) || after!=owner) return;
        if (impact) {
            if (impact->owner!=owner || input!=impact->source_current) return;
            render_camera=impact->camera;
        }
        // Keep the producer lease. A render-camera update cannot keep a
        // stopped or hidden character's old palette alive indefinitely.
        next->at_ms=producer->at_ms; next->sequence=producer->sequence;
        next->native_draw=next->render_export=true;
        next->render_camera=render_camera; next->camera_at_ms=GetTickCount64();
        std::lock_guard lock(s.mutex);
        if (s.watch.owner!=owner || s.watch.holder!=watch.holder || s.render_object!=object || GetTickCount64()>=s.watch.until) return;
        s.raster_pose=std::move(next);
        if (!s.view_logged) {
            s.view_logged=true;
            logging::log(logging::Level::info,logging::Channel::graphics,
                "X-ray raster view: current {:#x}, previous {:#x}, camera delta ({:.6f}, {:.6f}, {:.6f}), thread {}.",
                current_view,previous_view,delta[0],delta[1],delta[2],GetCurrentThreadId());
        }
    } catch (...) {}
}
bool install_view_hook(std::uintptr_t base) {
    auto& s=state(); std::array<unsigned char,32> bytes{};
    if (!read(base+layout::render_view.rva,bytes) || bytes!=layout::render_view.bytes) return false;
    bool camera_ok=true;
    for (const auto& contract : {layout::render_view_copy,layout::render_view_constructor})
        if (!read(base+contract.rva,bytes) || bytes!=contract.bytes) camera_ok=false;
    s.camera_contracts_ok.store(camera_ok,std::memory_order_release);
    void* original{};
    auto target=reinterpret_cast<void*>(base+layout::render_view.rva);
    if (hook_prepare(target,reinterpret_cast<void*>(&render_view_hook),&original)!=HookOk) return false;
    s.original_render_view=reinterpret_cast<decltype(s.original_render_view)>(original);
    return hook_enable(target)==HookOk;
}
bool contracts(std::uintptr_t base) {
    if (!no_bail_available()) return false;
    for (const auto& contract : {layout::contacts, layout::skeleton}) {
        std::array<unsigned char, 32> actual{};
        if (!read(base + contract.rva, actual) || actual != contract.bytes) return false;
    }
    return true;
}
bool capture(const LocalBailOwner& owner, float dt, bool bailed, Frame& frame, Snapshot& output) {
    frame = {};
    frame.entity = owner.entity;
    frame.world = owner.world;
    frame.dt = dt;
    frame.bailed = bailed;
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
    std::array<Joint,24> physics_joints{};
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
        const auto part = parts + i*layout::body_stride;
        std::uint8_t contact{};
        if (pointer(part + 0x10) != physics || !read(part + layout::velocity, body.velocity) ||
            !finite(body.velocity) || length(body.velocity) > 300 ||
            !read(contacts + layout::contact_flags + i, contact) || contact > 1) return false;
        body.contact = contact != 0;
        if (body.contact) {
            ++output.contacts;
            const auto valid_normal = [](const Vec3& normal) { return finite(normal) && length(normal) >= .5f && length(normal) <= 1.5f; };
            if (!read(contacts + layout::normals + i*layout::normal_stride, body.normal) || !valid_normal(body.normal)) {
                // Some accepted contacts have only the second normal, or no
                // detailed normal yet. Those cannot invalidate other bodies'
                // telemetry or create an impact without a verified direction.
                if (!read(contacts + layout::normals + i*layout::normal_stride + 0x10, body.normal) || !valid_normal(body.normal)) {
                    body.contact = false;
                    continue;
                }
            }
            frame.grounded = frame.grounded || body.normal[1]/length(body.normal) > .5f;
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
void publish_result(State& s) {
    s.published.result = s.challenge.result();
    if (s.published.xray_context_valid) s.published.visual_events.observe(s.published.result,GetTickCount64());
    else s.published.visual_events.reset();
    s.visible.store(s.published.visible, std::memory_order_release);
    s.published.normal_xray_result=s.normal_xray.result();
    s.published.normal_xray_events=s.normal_xray.events();
    s.draw_active.store(s.published.visible ||
        (s.published.visuals.normal_play && s.published.normal_xray_available),std::memory_order_release);
}
void unwatch(State& s) {
    s.rig.store(0, std::memory_order_release);
    s.selector.store(0, std::memory_order_release);
    s.animation_interface.store(0, std::memory_order_release);
    s.animation_component.store(0, std::memory_order_release);
    s.watch = {};
    s.pose_publication.invalidate();
    s.published.pose_valid = false;
    s.published.mesh_pose.reset();
    s.raster_pose.reset();
    s.render_object=0;
    s.published.xray_context_valid=false;
    s.published.normal_xray_available=false;
    s.normal_xray.reset();
    s.last.valid = false;
    s.bail_latched = false;
}
}
bool request(Action action) noexcept {
    try {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        if (s.pending) return false;
        s.pending = action;
        return true;
    } catch (...) { return false; }
}
Snapshot snapshot() { auto& s = state(); std::lock_guard lock(s.mutex); return s.published; }
Snapshot presentation_snapshot() {
    auto& s=state(); Snapshot result; Watch watch; std::uintptr_t object{};
    {
        std::lock_guard lock(s.mutex);
        result=s.published; watch=s.watch; object=s.render_object;
        if (s.view_hook_ok) result.mesh_pose=s.raster_pose;
        // The native draw pose has the game's render interpolation applied.
        // Raw animation exports arrive at simulation frequency and must not
        // replace it with a pose ahead of the rendered character.
        s.pose_publication.presented();
    }
    if (result.mesh_pose) {
        LocalBailOwner current;
        if (GetTickCount64()>=watch.until || GetTickCount64()-result.mesh_pose->at_ms>=250 ||
            (result.mesh_pose->render_camera && GetTickCount64()-result.mesh_pose->camera_at_ms>=250) ||
            !resolve_local_bail_owner(watch.client,watch.owner.entity,current) || current!=watch.owner) {
            result.mesh_pose.reset();
        } else if (result.mesh_pose->native_draw && !result.mesh_pose->render_camera && result.mesh && object) {
            std::uint32_t index{}; std::uint8_t visible{};
            if (pointer(object)!=current.base+layout::render_instance_vtable ||
                !read(object+0x108,index) || !read(object+0xff,visible) || visible!=1 ||
                (index!=local_render_index(watch,0) && index!=local_render_index(watch,1))) {
                result.mesh_pose.reset();
            } else {
                auto pose=std::make_shared<MeshPose>();
                if (refresh_render_pose(watch,object,*result.mesh,*pose)) {
                    LocalBailOwner after;
                    if (GetTickCount64()>=watch.until ||
                        !resolve_local_bail_owner(watch.client,watch.owner.entity,after) || after!=watch.owner) {
                        result.mesh_pose.reset();
                        return result;
                    }
                    // Preserve the draw callback's lease/age. Reading the live
                    // buffer must not make a stopped native producer look fresh.
                    pose->at_ms=result.mesh_pose->at_ms;
                    pose->sequence=result.mesh_pose->sequence;
                    pose->native_draw=pose->render_export=pose->presentation_read=true;
                    result.mesh_pose=std::move(pose);
                }
            }
        }
    }
    return result;
}
bool set_visual_options(const VisualOptions& options) noexcept {
    if (!valid_visual_options(options)) return false;
    try {
        auto& s=state(); std::lock_guard lock(s.mutex);
        if (!s.published.visual_options_ready) return false;
        if (s.published.visuals.normal_play!=options.normal_play) s.normal_xray.reset();
        s.published.visuals=options;
        publish_result(s);
        // Dragging a slider updates the visual immediately; save only after
        // the final change, on the game thread, as one profile transaction.
        s.visuals_save_at=GetTickCount64()+350;
        s.published.visual_save_status="Saving X-ray settings...";
        return true;
    } catch (...) {return false;}
}
bool set_challenge_config(const Config& config) noexcept {
    if (!valid_config(config)) return false;
    try {
        auto& s=state(); std::lock_guard lock(s.mutex);
        if (!s.published.challenge_options_ready) return false;
        if (s.published.selected_config==config) return true;
        s.published.selected_config=config;
        s.selected_best_identity.clear();
        s.config_save_at=GetTickCount64()+350;
        s.published.challenge_save_status="Saving challenge settings...";
        return true;
    } catch (...) {return false;}
}
bool set_bail_controls(const BailControls& controls) noexcept {
    if (!valid_bail_controls(controls)) return false;
    try {
        auto& s=state(); std::lock_guard lock(s.mutex);
        if (!s.published.bail_controls_ready) return false;
        s.published.bail_controls=controls;
        s.controls_save_at=GetTickCount64()+350;
        s.published.bail_controls_save_status="Saving bail controls...";
        return true;
    } catch (...) {return false;}
}
bool hud_visible() noexcept { return state().visible.load(std::memory_order_acquire); }
bool visuals_visible() noexcept { return state().draw_active.load(std::memory_order_acquire); }
void tick(std::uintptr_t base, std::uintptr_t client, std::uintptr_t entity, bool ready,
    bool offline, bool no_bail, bool noclip, bool editor, bool first_person, std::string_view map) noexcept {
    try {
        auto& s = state();
        LocalBailOwner owner;
        const bool owned = ready && offline && !noclip && !editor &&
            resolve_local_bail_owner(client, entity, owner);
        // Multiplayer exits before installing these hooks in solo play. Slam
        // must initialize the shared hooks on the client thread itself. Do
        // this before taking our mutex: installation touches native threads.
        if (owned && !s.pose_hooks_checked) {
            s.pose_hooks_checked = true;
            std::string detail;
            s.pose_hooks_ok = multiplayer::install_entity_hooks(base, detail);
            logging::log(s.pose_hooks_ok ? logging::Level::info : logging::Level::warning, logging::Channel::skater,
                "Slam overlay hooks: {}", s.pose_hooks_ok ? "ready for offline animation capture" : detail);
        }
        if (owned && !s.render_hook_checked) {
            s.render_hook_checked=true;
            s.render_hook_ok=install_render_hook(base);
            logging::log(logging::Level::info,logging::Channel::graphics,"X-ray native presentation hook: {}.",s.render_hook_ok ? "ready" : "unavailable");
        }
        if (owned && !s.cache_hook_checked) {
            s.cache_hook_checked=true;
            s.cache_hook_ok=install_cache_hook(base);
            logging::log(logging::Level::info,logging::Channel::graphics,"X-ray renderer cache observer: {}.",s.cache_hook_ok ? "ready" : "unavailable");
        }
        if (owned && !s.view_hook_checked) {
            s.view_hook_checked=true;
            s.view_hook_ok=install_view_hook(base);
            logging::log(logging::Level::info,logging::Channel::graphics,"X-ray raster view observer: {}.",s.view_hook_ok ? "ready" : "unavailable");
        }
        std::unique_lock lock(s.mutex);
        s.published.image_base = base;
        s.published.first_person=first_person;
        constexpr std::string_view config_key="Slam.Challenge.v1";
        constexpr std::string_view visuals_key="Slam.Visuals.v1";
        constexpr std::string_view controls_key="Slam.Controls.v1";
        const BestBook::Read read_best=[](std::string_view key)->std::optional<std::string> {
            const auto saved=profile_runtime::local_value(key);
            return saved && saved->is_string() ? std::optional(saved->string()) : std::nullopt;
        };
        const BestBook::Write write_best=[](std::string_view key,std::string_view document) {
            const std::string encoded(document);
            profile_runtime::set_local_values({{std::string(key),encoded}});
            const auto saved=profile_runtime::local_value(key);
            return saved && saved->is_string() && saved->string()==encoded;
        };
        if (owned && !s.published.challenge_options_ready) {
            if (const auto saved=read_best(config_key)) {
                if (const auto config=decode_config(*saved)) {
                    s.published.selected_config=*config;
                    s.published.challenge_save_status="Saved challenge settings restored.";
                } else s.published.challenge_save_status="Saved challenge settings could not be read; using defaults.";
            }
            s.published.challenge_options_ready=true;
        }
        if (owned && !s.published.bail_controls_ready) {
            if (const auto saved=read_best(controls_key)) {
                if (const auto controls=decode_bail_controls(*saved)) {
                    s.published.bail_controls=*controls;
                    s.published.bail_controls_save_status="Saved bail controls restored.";
                } else s.published.bail_controls_save_status="Saved bail controls could not be read; using defaults.";
            }
            s.published.bail_controls_ready=true;
        }
        // The physics hook only queues an immutable completed result. Drain
        // it here before processing a new start or a map/owner transition.
        if (offline && ready) {
            for (const auto& attempt : s.completed_attempts) {
                const auto recorded=s.best_book.record(attempt.serial,attempt.map,attempt.result,read_best,write_best);
                if (!recorded) continue;
                s.published.result_best=recorded->best;
                s.published.result_best_ready=true;
                s.published.new_best=recorded->improved;
                s.selected_best_identity.clear();
                if (!recorded->saved) s.best_retry_at=GetTickCount64()+5000;
                s.published.best_save_status=s.best_retry_at ? "Personal best kept for this session; retrying save." : "Personal best saved.";
                logging::log(logging::Level::info,logging::Channel::skater,
                    "Slam result recorded: attempt {}, {}, target {:.2f}, {} points, best {} points, attempts {}, saved={}. {}",
                    attempt.serial,challenge_name(attempt.result.config.kind),attempt.result.config.target,attempt.result.points,
                    recorded->best.score,recorded->best.attempts,recorded->saved,s.published.best_save_status);
            }
            s.completed_attempts.clear();
            if (s.controls_save_at && GetTickCount64()>=s.controls_save_at) {
                s.controls_save_at=0;
                const bool saved=write_best(controls_key,encode_bail_controls(s.published.bail_controls));
                s.published.bail_controls_save_status=saved ? "Bail controls saved." : "Could not save bail controls; changes apply for this session.";
            }
            if (s.best_retry_at && GetTickCount64()>=s.best_retry_at) {
                const bool saved=s.best_book.flush(write_best);
                s.best_retry_at=saved ? 0 : GetTickCount64()+5000;
                s.published.best_save_status=saved ? "Personal best saved." : "Personal best kept for this session; retrying save.";
            }
            if (s.config_save_at && GetTickCount64()>=s.config_save_at) {
                s.config_save_at=0;
                const bool saved=write_best(config_key,encode_config(s.published.selected_config));
                s.published.challenge_save_status=saved ? "Challenge settings saved." : "Could not save challenge settings; changes apply for this session.";
            }
        }
        if (owned && !map.empty() && map.size()<=512 && map.find('\0')==std::string_view::npos) {
            const auto identity=std::string(map)+"\n"+encode_config(s.published.selected_config);
            if (identity!=s.selected_best_identity) {
                s.published.selected_best=s.best_book.lookup(map,s.published.selected_config,read_best).best;
                s.selected_best_identity=identity;
                logging::log(logging::Level::info,logging::Channel::skater,
                    "Slam challenge selected: {}, target {:.2f}, best {} points, {} completed attempts.",
                    challenge_name(s.published.selected_config.kind),s.published.selected_config.target,
                    s.published.selected_best.score,s.published.selected_best.attempts);
            }
        }
        if (owned && !s.published.visual_options_ready) {
            if (const auto saved=profile_runtime::local_value(visuals_key)) {
                const auto decoded=saved->is_string() ? decode_visual_options(saved->string()) : std::nullopt;
                if (decoded) {
                    s.published.visuals=*decoded; s.published.visual_save_status="Saved X-ray settings restored.";
                    logging::log(logging::Level::info,logging::Channel::skater,
                        "Slam X-ray settings restored: visibility={}, opacity={:.2f}, reduced effects={}, duration={:.1f}s, normal play={}.",
                        xray_visibility_name(decoded->visibility),decoded->opacity,decoded->reduced_effects,decoded->impact_duration_s,decoded->normal_play);
                }
                else s.published.visual_save_status="Saved X-ray settings could not be read; using defaults.";
            }
            s.published.visual_options_ready=true;
        }
        if (s.normal_play_observed!=s.published.visuals.normal_play) {
            s.normal_play_observed=s.published.visuals.normal_play;
            logging::log(logging::Level::info,logging::Channel::skater,"Normal-play X-ray {}.",s.normal_play_observed ? "enabled" : "disabled");
        }
        if (offline && ready && s.visuals_save_at && GetTickCount64()>=s.visuals_save_at) {
            s.visuals_save_at=0;
            const auto encoded=encode_visual_options(s.published.visuals);
            profile_runtime::set_local_values({{std::string(visuals_key),encoded}});
            const auto saved=profile_runtime::local_value(visuals_key);
            const bool confirmed=saved && saved->is_string() && saved->string()==encoded;
            s.published.visual_save_status=confirmed ? "X-ray settings saved." : "Could not save X-ray settings; changes apply for this session.";
            logging::log(confirmed ? logging::Level::info : logging::Level::warning,logging::Channel::skater,
                "Slam X-ray settings: {}",s.published.visual_save_status);
        }
        if (!s.contracts_checked && no_bail_available()) { s.contracts_ok = contracts(base); s.contracts_checked = true; }
        const char* visual_issue = !offline ? "X-ray requires offline play." :
            noclip ? "Turn off Noclip before starting an attempt." :
            editor ? "Exit the park editor before starting an attempt." :
            !s.contracts_ok ? "Slam telemetry is unavailable for this game build." :
            !owned ? "Waiting for an offline local skater." :
            !s.pose_hooks_ok ? "Slam skeleton overlay is unavailable for this game build." : nullptr;
        const bool changed = s.watch.owner.entity && (owner != s.watch.owner || map != s.map);
        const char* issue = visual_issue ? visual_issue : no_bail ? "Turn off No Bail before starting an attempt." : nullptr;
        if (visual_issue || changed) {
            s.challenge.cancel(visual_issue ? visual_issue : "Attempt cancelled: the skater or map changed.");
            unwatch(s);
        }
        if (no_bail) {
            s.challenge.cancel(issue);
            s.published.xray_context_valid=false;
        }
        if (!visual_issue) {
            const auto component = pointer(owner.entity + 0x628);
            const auto holder = pointer(component + 0xa0);
            s.watch = {owner, client, component, holder, GetTickCount64() + 500};
            s.animation_interface.store(holder ? holder + 0xc0 : 0, std::memory_order_release);
            s.animation_component.store(component, std::memory_order_release);
            s.rig.store(owner.rig, std::memory_order_release);
            s.selector.store(owner.selector, std::memory_order_release);
        }
        s.published.normal_xray_available=!visual_issue;
        s.map = map;
        s.published.available = !issue && s.last.valid && GetTickCount64() - s.last_at < 250;
        if (issue) s.published.availability = issue;
        bool explicit_bail=false;
        if (s.pending) {
            const auto action = *s.pending;
            s.pending.reset();
            if (action == Action::dismiss) { s.challenge.cancel("Attempt dismissed."); s.retry.cancel(); s.published.visible = false; cancel_manual_bail(); }
            else if (action == Action::stop) {s.challenge.cancel("Attempt stopped."); s.retry.cancel(); cancel_manual_bail();}
            else if (action == Action::bail) explicit_bail=true;
            else if (action == Action::retry) {
                // Queued here, dispatched below with no Slam mutex held.
                RetryObservation frame;
                frame.owner=owner; frame.client=client; frame.entity=entity; frame.world=pointer(client+8); frame.map=map;
                frame.generation=s.level_generation.load(); frame.now=GetTickCount64();
                frame.allowed=!issue && !first_person && !s.effects_suspended.load();
                frame.owned=owned; frame.fresh=s.published.available; frame.bailed=s.last.bailed;
                if (!s.challenge.running() && s.retry.begin(frame)) {
                    s.published.visible=true; cancel_manual_bail();
                    s.published.visual_events.reset(); s.normal_xray.reset();
                } else s.published.availability="Recover before retrying; the saved start must belong to this skater and map.";
            }
            else {
                s.published.visible = true;
                if (!s.challenge.running() && !s.retry.active() && s.published.available && s.published.challenge_options_ready &&
                    !map.empty() && map.size()<=512 && map.find('\0')==std::string_view::npos &&
                    s.challenge.begin(s.last,s.published.selected_config)) {
                    ++s.attempt_serial; s.attempt_map=map;
                    s.published.result_best_ready=false; s.published.new_best=false;
                    s.published.xray_context_valid=true;
                    s.published.visual_events.reset();
                    PoseMatrix start{};
                    s.retry.invalidate("Starting transform could not be saved; start a new attempt here.");
                    if (read_actor_world(owner,start)) {
                        // Entity transform's SIMD metadata lanes are not
                        // position/orientation; native teleport needs affine W.
                        start[3]=start[7]=start[11]=0; start[15]=1;
                        (void)s.retry.save({owner,client,s.level_generation.load(),std::string(map),start,s.published.selected_config});
                    }
                    logging::log(logging::Level::info, logging::Channel::skater,"Slam attempt {} started: {}, target {:.2f} {}.",
                        s.attempt_serial,challenge_name(s.published.selected_config.kind),s.published.selected_config.target,
                        challenge_unit(s.published.selected_config.kind));
                } else {
                    s.published.availability = s.last.bailed ? "Recover and get back on your board before retrying." :
                        s.challenge.running() ? "Cancel the current attempt before starting another." :
                        s.retry.active() ? "Wait for the saved-start retry or cancel it." : issue ? issue : "Waiting for a fresh physics sample.";
                }
            }
        }
        RetryObservation retry_frame;
        retry_frame.owner=owner; retry_frame.client=client; retry_frame.entity=entity; retry_frame.world=pointer(client+8); retry_frame.map=map;
        retry_frame.generation=s.level_generation.load(); retry_frame.now=GetTickCount64(); retry_frame.sample_at=s.last_at;
        // Ownership can be temporarily unavailable during our native teleport.
        // Only the submitted retry may wait through that gap; it cannot dispatch.
        retry_frame.allowed=offline && !no_bail && !noclip && !editor && !first_person && !s.effects_suspended.load();
        retry_frame.owned=owned; retry_frame.fresh=s.published.available; retry_frame.bailed=s.last.bailed;
        retry_frame.pose_valid=owned && read_actor_world(owner,retry_frame.transform);
        if (retry_frame.pose_valid) {retry_frame.transform[3]=retry_frame.transform[7]=retry_frame.transform[11]=0; retry_frame.transform[15]=1;}
        if (s.retry.saved() && !s.retry.retains_start(retry_frame))
            s.retry.invalidate("Saved start cleared: the skater or map changed.");
        const auto retry_step=s.retry.step(retry_frame,s.retry.returning() ? inspect_skater_teleport(s.retry.receipt()) : SkaterTeleportState::busy);
        std::optional<SavedStart> retry_dispatch;
        if (retry_step==RetryStep::dispatch) retry_dispatch=s.retry.saved();
        if (retry_step==RetryStep::arrived && s.retry.saved()) {
            const auto& start=*s.retry.saved();
            if (s.challenge.begin(s.last,start.config)) {
                ++s.attempt_serial; s.attempt_map=map;
                s.published.result_best_ready=false; s.published.new_best=false;
                s.published.xray_context_valid=true; s.published.visual_events.reset();
                logging::log(logging::Level::info,logging::Channel::skater,
                    "Slam retry arrived and attempt {} started: {}, actor {:.3f}, {:.3f}, {:.3f}, forward {:.3f}, {:.3f}, {:.3f}.",
                    s.attempt_serial,challenge_name(start.config.kind),retry_frame.transform[12],retry_frame.transform[13],retry_frame.transform[14],
                    retry_frame.transform[8],retry_frame.transform[9],retry_frame.transform[10]);
            } else s.retry.cancel("Returned to the start, but the attempt could not begin. Try again after recovery.");
        }
        s.published.saved_start=s.retry.saved().has_value();
        s.published.retry_active=s.retry.active();
        s.published.retry_available=!s.challenge.running() && s.retry.available(retry_frame);
        s.published.retry_status=s.retry.status();
        const auto& controls=s.published.bail_controls;
        const auto keys=launcher::overlay_keys();
        s.published.bail_key_conflict=controls.key && (controls.key==keys.menu || controls.key==keys.console);
        const auto bindings=local_profile_controller_bindings();
        s.published.bail_controller_conflict=overlapping_combos(controls.controller_combo,bindings.noclip_combo) ||
            overlapping_combos(controls.controller_combo,bindings.forward_velocity_combo) ||
            overlapping_combos(controls.controller_combo,bindings.up_velocity_combo);
        const bool bail_mode=s.published.bail_controls_ready && controls.enabled &&
            (s.challenge.running() || s.published.visuals.normal_play);
        const char* bail_issue=s.retry.active() ? "Wait for retry to return to the saved start." :
            !bail_mode ? "Start an attempt or enable normal-play X-ray and Manual bail." :
            issue ? issue : changed ? "Waiting for the current skater and map." :
            first_person ? "Turn off First person before using Manual bail." :
            s.effects_suspended.load(std::memory_order_acquire) ? "Waiting for loading to finish." :
            !s.last.valid || GetTickCount64()-s.last_at>=250 ? "Waiting for a fresh physics sample." :
            s.last.bailed ? "Recover before bailing again." :
            !manual_bail_state(s.published.physics_state) ? "Wait for normal skating or walking before bailing." : nullptr;
        s.published.bail_available=!bail_issue;
        s.published.bail_status=bail_issue ? bail_issue : "Manual bail is ready.";
        const bool window_focused=game_window_focused();
        const bool shortcuts=window_focused && overlay::keyboard_shortcuts_allowed();
        bool key_down{};
        {
            overlay::detail::OverlayInputAccess access;
            if (controls.key) key_down=(GetAsyncKeyState(static_cast<int>(controls.key))&0x8000)!=0;
        }
        ControllerInput controller;
        DingoSDKOverlayReadControllerInput(&controller);
        const bool key_pressed=s.bail_key.update(controls.key,key_down,
            !shortcuts || bail_issue || s.published.bail_key_conflict);
        const bool pad_pressed=s.bail_controller.update(controls.controller_combo,controller,
            !shortcuts || bail_issue || !bindings.available || s.published.bail_controller_conflict);
        const bool bail_context=bail_mode && !issue && !changed && !first_person &&
            !s.effects_suspended.load(std::memory_order_acquire);
        if (!bail_context || !window_focused || (!shortcuts && GetTickCount64()>=s.explicit_bail_until)) cancel_manual_bail();
        if (explicit_bail || key_pressed || pad_pressed) {
            const bool accepted=!bail_issue && window_focused && queue_manual_bail(client,entity);
            if (accepted && explicit_bail) s.explicit_bail_until=GetTickCount64()+500;
            s.published.bail_status=accepted ? "Manual bail queued for native physics and animation." :
                bail_issue ? bail_issue : "Manual bail could not be queued; keep the game focused and try again.";
            logging::log(accepted ? logging::Level::info : logging::Level::warning,logging::Channel::skater,"{}",s.published.bail_status);
            if (!accepted) overlay::notify(overlay::NoticeLevel::warning,"Manual bail",s.published.bail_status);
        }
        const auto manual=manual_bail_status();
        s.published.manual_bails_queued=manual.queued;
        s.published.manual_bails_selected=manual.selected;
        s.published.manual_bails_published=manual.published;
        if ((s.published.visible || s.published.visuals.normal_play) && !s.mesh_started && owned) {
            s.mesh_started=true;
            s.published.mesh_status="Loading Dem Bones from the installed game...";
            try {
                std::wstring executable(32768,L'\0');
                const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
                if (!count || count>=executable.size()) throw std::runtime_error("Cannot locate installed game assets");
                executable.resize(count);
                s.mesh_load=std::async(std::launch::async,[root=std::filesystem::path(executable).parent_path()] {
                    return std::make_shared<const SkeletonMesh>(load_dembones_mesh(root));
                });
            } catch (const std::exception& error) {
                s.published.mesh_status=std::string("Dem Bones X-ray unavailable: ")+error.what();
                logging::write(logging::Level::warning,logging::Channel::skater,s.published.mesh_status);
            }
        }
        if (s.mesh_load.valid() && s.mesh_load.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
            try {
                s.published.mesh=s.mesh_load.get();
                s.published.mesh_status="Dem Bones 3D X-ray mesh ready.";
                logging::log(logging::Level::info,logging::Channel::skater,"Slam X-ray: Dem Bones loaded ({} vertices, {} triangles).",
                    s.published.mesh->vertices.size(),s.published.mesh->indices.size()/3);
            } catch (const std::exception& error) {
                s.published.mesh_status=std::string("Dem Bones X-ray unavailable: ")+error.what();
                logging::write(logging::Level::warning,logging::Channel::skater,s.published.mesh_status);
            }
        }
        if (s.challenge.running() && GetTickCount64() - s.last_at > 500)
            s.challenge.cancel("Attempt cancelled: physics telemetry stopped.");
        if (GetTickCount64()-s.last_at>500) s.normal_xray.reset();
        publish_result(s);
        const auto effects_options=s.published.visuals;
        const auto effects_events=effects_options.normal_play ? s.published.normal_xray_events : s.published.visual_events;
        const bool effects_allowed=!visual_issue && !s.effects_suspended.load(std::memory_order_acquire) &&
            !s.retry.active() &&
            !s.effects_cancel_requested.exchange(false,std::memory_order_acq_rel) && GetTickCount64()-s.last_at<250 &&
            (effects_options.normal_play || (s.published.visible && s.published.xray_context_valid));
        const bool camera_allowed=effects_allowed && !first_person && gameplay_focused();
        s.camera_impact=s.camera_pulse.step(effects_options,effects_events,GetTickCount64(),camera_allowed);
        if (!camera_allowed) {s.previous_camera_motion={}; s.previous_effect_camera=0;}
        s.published.impact_camera_available=s.view_hook_ok && s.camera_contracts_ok.load(std::memory_order_acquire);
        // Native setting listeners can call back into game systems. Invoke
        // them with no Slam mutex held; presentation only reads the result.
        lock.unlock();
        SkaterTeleportSubmission retry_submission;
        if (retry_dispatch && retry_dispatch->generation==s.level_generation.load() && ready && offline &&
            !no_bail && !noclip && !editor && !first_person && !s.effects_suspended.load())
            retry_submission=submit_owned_skater_teleport(client,retry_dispatch->owner,retry_dispatch->transform);
        const auto pulse=update_time_effect(effects_options,effects_events,effects_allowed && !first_person && gameplay_focused());
        const auto audio_status=update_impact_audio(effects_options,effects_events,effects_allowed);
        lock.lock();
        if (retry_dispatch && s.retry.active() && retry_dispatch->generation==s.level_generation.load()) {
            if (retry_submission.state==SkaterTeleportState::submitted) {
                s.retry.submitted(retry_submission.receipt,GetTickCount64());
                // Fresh samples must come from after the native handoff.
                s.last.valid=false; s.published.available=false;
            } else if (retry_submission.state==SkaterTeleportState::unavailable)
                s.retry.cancel("Saved-start teleport is unavailable for this skater. Recover and try again.");
            s.published.retry_active=s.retry.active(); s.published.retry_status=s.retry.status();
        }
        s.published.impact_audio_status=audio_status;
        s.published.slow_motion_active=pulse.active;
        s.published.slow_motion_factor=pulse.factor;
        s.published.slow_motion_status=s.time_status;
        if (s.diagnostic != s.published.availability) {
            s.diagnostic = s.published.availability;
            logging::log(logging::Level::info, logging::Channel::skater, "Slam telemetry: {} ({} samples, {} rejected)",
                s.diagnostic, s.published.samples, s.published.dropped);
        }
        if (s.retry_diagnostic!=s.retry.status()) {
            s.retry_diagnostic=s.retry.status();
            logging::log(logging::Level::info,logging::Channel::skater,
                "Slam retry: {} (active={}, owned={}, entity={:x}, world={:x}, generation={}).",
                s.retry_diagnostic,s.retry.active(),owned,entity,retry_frame.world,retry_frame.generation);
        }
    } catch (...) {
        cancel_manual_bail();
        (void)restore_impact_time_scale();
    }
}
void before_level_transition(unsigned next) noexcept {
    try {
        auto& s=state();
        if (next==3 || next==14 || next==22 || next==24 || next==25) {
            s.level_generation.fetch_add(1,std::memory_order_acq_rel);
            cancel_manual_bail();
            s.effects_suspended.store(true,std::memory_order_release);
            s.effects_cancel_requested.store(true,std::memory_order_release);
            // The settings bridge only writes on its owning game thread. If
            // this callback runs elsewhere, the next client tick retries it.
            (void)restore_impact_time_scale();
        } else if (next==13 || next==21) s.effects_suspended.store(false,std::memory_order_release);
    } catch (...) {}
}
void observe_selection(std::uintptr_t selector, std::uint32_t next) noexcept {
    auto& s = state();
    if (!selector || selector != s.selector.load(std::memory_order_acquire)) return;
    try {
        std::lock_guard lock(s.mutex);
        if (selector == s.watch.owner.selector && GetTickCount64() < s.watch.until && next == 300) s.bail_latched = true;
    } catch (...) {}
}
void observe_manual_bail(std::uintptr_t selector) noexcept {
    auto& s=state();
    if (!selector || selector!=s.selector.load(std::memory_order_acquire)) return;
    try {
        std::lock_guard lock(s.mutex);
        if (selector==s.watch.owner.selector && GetTickCount64()<s.watch.until) {
            s.bail_latched=true;
            s.manual_bail_at=GetTickCount64();
        }
    } catch (...) {}
}
void observe_skeleton(std::uintptr_t rig, float seconds, bool wipeout) noexcept {
    auto& s = state();
    if (!rig || rig != s.rig.load(std::memory_order_acquire)) return;
    try {
        Watch watch;
        { std::lock_guard lock(s.mutex); watch = s.watch; }
        LocalBailOwner current;
        if (rig != watch.owner.rig || GetTickCount64() >= watch.until ||
            !resolve_local_bail_owner(watch.client, watch.owner.entity, current) || current != watch.owner) return;
        std::lock_guard lock(s.mutex);
        if (s.watch.owner != current || GetTickCount64() >= s.watch.until) return;
        if (!std::isfinite(seconds) || seconds <= 0 || seconds > .1f) return;
        std::uint32_t selected{};
        if (!read(current.context + 0x1414, selected)) return;
        s.bail_latched = s.bail_latched || wipeout;
        // 504 covers walking and falling. A verified walking ground substate
        // can recover without mounting. Give a published request time to reach
        // animation before considering that same initial walking pose recovered.
        if (!wipeout && ((selected >= 100 && selected < 300) || (selected >= 400 && selected < 500))) s.bail_latched = false;
        if (!wipeout && s.bail_latched && GetTickCount64()-s.manual_bail_at>750 &&
            local_bail_recovered(current)) s.bail_latched=false;
        Frame frame;
        (void)capture(current, seconds, s.bail_latched, frame, s.published);
        ++s.published.samples;
        if (!frame.valid) { ++s.published.dropped; s.published.available = false; }
        else {
            s.last = frame;
            s.last_at = GetTickCount64();
            s.published.available = true;
            s.published.bailed = frame.bailed;
            s.published.availability = frame.bailed ? "Recover and get back on your board before retrying." : "Ready to start an attempt.";
        }
        const auto previous_phase = s.challenge.result().phase;
        s.challenge.step(frame);
        if (s.published.visuals.normal_play) {
            const auto previous_impacts=s.normal_xray.result().impacts;
            s.normal_xray.step(frame,GetTickCount64());
            if (s.normal_xray.result().impacts>previous_impacts)
                logging::log(logging::Level::info,logging::Channel::skater,"Normal-play X-ray: {} impacts tracked this fall.",s.normal_xray.result().impacts);
        }
        if (s.challenge.result().phase == Phase::results && previous_phase != Phase::results) {
            if (!s.challenge.result().cancelled && !s.attempt_map.empty())
                s.completed_attempts.push_back({s.attempt_serial,s.attempt_map,s.challenge.result()});
            logging::log(logging::Level::info, logging::Channel::skater, "Slam Challenge: {} points, {} impacts, {} fractures. {}",
                s.challenge.result().points, s.challenge.result().impacts, s.challenge.result().fractures, s.challenge.result().detail);
        }
        publish_result(s);
    } catch (...) {}
}
namespace {
void observe_pose(std::uintptr_t component, std::uintptr_t render_data) noexcept {
    auto& s = state();
    if (!s.draw_active.load(std::memory_order_acquire) || !component ||
        component != s.animation_component.load(std::memory_order_acquire)) return;
    const auto error = GetLastError();
    try {
        Watch watch;
        std::uint64_t ticket{};
        std::shared_ptr<const SkeletonMesh> mesh;
        { std::lock_guard lock(s.mutex); watch = s.watch; mesh=s.published.mesh; ticket=s.pose_publication.issue(); }
        LocalBailOwner current;
        if (GetTickCount64() >= watch.until || component != watch.component ||
            !resolve_local_bail_owner(watch.client, watch.owner.entity, current) || current != watch.owner ||
            pointer(current.entity + 0x628) != watch.component || pointer(watch.component + 0xa0) != watch.holder) {
            SetLastError(error);
            return;
        }
        const auto pose = multiplayer::read_native_pose_layout(memory::peek_bytes, current.base, watch.holder, 395);
        if (!pose.buffer || pose.count != 395 || (render_data && pointer(render_data + 0x10) != pose.buffer))
            throw std::runtime_error("Skater render pose differs from animation output.");
        // Every joint and skinning matrix uses this single complete copy.
        // Reading the 26 diagnostic joints separately could mix evaluations.
        std::array<std::array<float,12>,render_bone_count> bones{};
        if (!read(pose.buffer,bones)) throw std::runtime_error("Full player render pose is unavailable.");
        if (render_data) {
            std::array<std::array<float,12>,render_bone_count> verified{};
            if (!read(pose.buffer,verified) || std::memcmp(bones.data(),verified.data(),sizeof(bones))!=0)
                throw std::runtime_error("Skater bones changed during export capture.");
        }
        const auto after=multiplayer::read_native_pose_layout(memory::peek_bytes,current.base,watch.holder,395);
        if (after.buffer!=pose.buffer || after.count!=pose.count)
            throw std::runtime_error("Skater output changed during pose capture.");
        std::array<PoseMatrix,26> world{};
        std::array<Joint,24> joints{};
        for (std::size_t i=0; i<world.size(); ++i) {
            const auto& bone=bones[static_cast<std::size_t>(render_joint_ids[i])];
            const auto parent = render_parent_indices[i];
            const auto composed = append_pose(parent < 0 ? identity_pose : world[static_cast<std::size_t>(parent)], bone);
            if (!composed) throw std::runtime_error("Skater render joint is invalid.");
            world[i] = *composed;
        }
        const Vec3 hips{world[2][12],world[2][13],world[2][14]};
        std::shared_ptr<MeshPose> mesh_pose;
        // Raw exports remain a fallback for builds without a native draw hook.
        // They cannot replace the interpolated articulation used by the game.
        if (mesh && render_data && !s.render_hook_ok) {
            mesh_pose=std::make_shared<MeshPose>();
            if (!skin_render_pose(*mesh,bones,*mesh_pose)) throw std::runtime_error("Full player skinning pose is invalid.");
            mesh_pose->at_ms=GetTickCount64();
            mesh_pose->sequence=ticket;
            mesh_pose->render_export=render_data!=0;
        }
        for (std::size_t i=0; i<joints.size(); ++i) {
            const auto index = static_cast<std::size_t>(overlay_joint_indices[i]);
            auto& joint = joints[i];
            joint.position = {world[index][12],world[index][13],world[index][14]};
            joint.parent = overlay_parents[i];
            joint.region = *region_for_joint(render_joint_ids[index]);
            const Vec3 delta{joint.position[0]-hips[0],joint.position[1]-hips[1],joint.position[2]-hips[2]};
            if (!finite(joint.position) || length(delta) > 6) throw std::runtime_error("Skater render pose failed its shape check.");
        }
        std::lock_guard lock(s.mutex);
        if (s.watch.owner == current && s.watch.holder == watch.holder && GetTickCount64() < s.watch.until) {
            if (render_data) ++s.published.export_poses;
            else ++s.published.animation_poses;
            if (!s.pose_publication.publish(ticket,render_data!=0)) {++s.published.superseded_poses; SetLastError(error); return;}
            s.published.joints = joints;
            if (mesh_pose) {
                s.published.mesh_pose=std::move(mesh_pose);
            }
            s.published.pose_valid = true;
            s.published.pose_at_ms = GetTickCount64();
            ++s.published.rendered_poses;
            if (s.published.rendered_poses == 1) {
                logging::write(logging::Level::info, logging::Channel::skater, "Slam overlay: final animation pose captured.");
            }
        }
    } catch (const std::exception& issue) {
        std::lock_guard lock(s.mutex);
        ++s.published.rejected_poses;
        if (s.pose_diagnostic != issue.what()) {
            s.pose_diagnostic = issue.what();
            logging::log(logging::Level::info, logging::Channel::skater, "Slam overlay: {}", s.pose_diagnostic);
        }
    } catch (...) {
        std::lock_guard lock(s.mutex);
        ++s.published.rejected_poses;
    }
    SetLastError(error);
}
}
void observe_animation(std::uintptr_t component) noexcept { observe_pose(component, 0); }
void observe_render(std::uintptr_t animation_interface, std::uintptr_t render_data,std::uintptr_t caller) noexcept {
    auto& s = state();
    if (animation_interface && animation_interface == s.animation_interface.load(std::memory_order_acquire)) {
        {std::lock_guard lock(s.mutex);
            if (!s.render_caller_logged) {
                s.render_caller_logged=true;
                logging::log(logging::Level::info,logging::Channel::graphics,"X-ray native render submission caller: Skate+{:#x}.",caller-s.published.image_base);
            }
        }
        observe_pose(s.animation_component.load(std::memory_order_acquire), render_data);
    }
}
}
