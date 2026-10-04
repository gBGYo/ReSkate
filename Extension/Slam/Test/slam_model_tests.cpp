#include "Extension/Slam/slam_model.h"
#include "Extension/Slam/slam_telemetry.h"
#include "Extension/Slam/slam_pose.h"
#include "Extension/Slam/slam_mesh.h"
#include "Extension/Slam/slam_pose_publication.h"
#include "Extension/Slam/slam_progression.h"
#include "Engine/Game/UI/live_game_view.h"
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dingosdk::slam;
int failures{};
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
Frame skater() {
    Frame f;
    f.valid = true; f.entity = 11; f.world = 22; f.dt = .02f;
    f.center = {0, 10, 0}; f.body_count = 2;
    f.bodies[0].region = Region::head;
    f.bodies[1].region = Region::left_arm;
    return f;
}
void fall(Frame& f, Challenge& c, float speed) {
    for (auto& b : f.bodies) { b.velocity = {0, -speed, 0}; b.contact = false; }
    c.step(f);
}
void hit(Frame& f, Challenge& c, std::size_t body) {
    f.bailed = true;
    f.grounded = true;
    f.bodies[body].contact = true;
    f.bodies[body].normal = {0, 1, 0};
    f.bodies[body].velocity = {};
    c.step(f);
}
void lifecycle() {
    Challenge c;
    auto f = skater();
    check(c.result().phase == Phase::ready, "A new challenge is Ready");
    check(c.begin(f), "A healthy skater can begin");
    check(c.result().phase == Phase::attempt, "Beginning enters Attempt");
    fall(f, c, 20);
    f.center[1] -= .4f;
    hit(f, c, 0);
    check(c.result().phase == Phase::bailed && c.result().impacts == 1, "First collision enters Bailed and scores");
    check(c.result().injuries[0].fractured && c.result().fractures == 1, "A severe head impact fractures once");
    check(c.result().fall_m > .39f && c.result().airtime_s > 0, "Fall and airtime carry across the bail transition");
    f.bodies[1].velocity = {};
    for (int i = 0; i < 45; ++i) c.step(f);
    check(c.result().phase == Phase::settled, "A resting ragdoll enters Settled");
    for (int i = 0; i < 20; ++i) c.step(f);
    check(c.result().phase == Phase::results && !c.result().cancelled, "Settled enters Results");
    const auto result = c.result();
    check(result.points == result.impact_points + result.bonus_points, "Final score has one metric bonus");
    for (int i = 0; i < 100; ++i) c.step(f);
    check(c.result().points == result.points, "Results are immutable while the skater remains down");
    check(!c.begin(f), "Retry cannot start in the middle of a bail");
    f.bailed = false;
    check(c.begin(f) && c.result().points == 0 && c.result().fractures == 0, "Retry clears old injuries and points");
}
void contact_scoring() {
    Challenge c;
    auto f = skater();
    c.begin(f);
    fall(f, c, 20);
    f.bailed = true;
    f.bodies[0].velocity = {};
    c.step(f);
    check(c.result().impacts == 0, "A large animation/velocity change without a contact never scores");
    fall(f, c, 20);
    hit(f, c, 0);
    const auto first = c.result();
    for (int i = 0; i < 12; ++i) { fall(f, c, 20); hit(f, c, 0); }
    check(c.result().fractures == 1, "Repeated hits cannot award another fracture for the same region");
    check(c.result().fracture_points == first.fracture_points, "Fracture bonus is awarded only once");
    check(c.result().impacts < 13, "Impact cooldown prevents callback-sized repeated awards");
    fall(f, c, 20);
    hit(f, c, 1);
    check(c.result().injuries[static_cast<std::size_t>(Region::left_arm)].fractured,
          "Another region can independently fracture");
    check(c.result().fractures == 2, "Injuries are attributed per body region");
}
void same_region() {
    Challenge c;
    auto f = skater();
    f.bodies[1].region = Region::head;
    c.begin(f); fall(f, c, 20);
    f.bailed = true; f.grounded = true;
    for (auto& b : f.bodies) { b.contact = true; b.normal = {0,1,0}; b.velocity = {}; }
    c.step(f);
    check(c.result().impacts == 1, "Several bodies of one region hitting in one step score as one impact");
}
void invalidation() {
    for (int kind = 0; kind < 5; ++kind) {
        Challenge c;
        auto f = skater(); c.begin(f); fall(f, c, 20); hit(f, c, 0);
        if (kind == 0) ++f.entity;
        if (kind == 1) ++f.world;
        if (kind == 2) f.center[0] += 100;
        if (kind == 3) f.valid = false;
        if (kind == 4) f.bodies[0].velocity[0] = std::numeric_limits<float>::quiet_NaN();
        for (int i = 0; i < 30; ++i) c.step(f);
        check(c.result().cancelled && c.result().phase == Phase::results && c.result().points == 0,
              "Changed skater/world, teleport, missing samples, and NaN cannot produce a completed score");
    }
    Challenge c;
    auto f = skater(); c.begin(f); fall(f, c, 20);
    f.dt = .3f; c.step(f);
    f.dt = .02f; hit(f, c, 0);
    check(c.result().impacts == 0, "No impact may span a dropped sample");
}
void free_fall_and_rest() {
    Challenge c;
    auto f = skater(); c.begin(f);
    f.bailed = true;
    // Gravity should still not score if a stale contact flag is present.
    for (int i = 0; i < 50; ++i) {
        f.bodies[0].contact = true; f.bodies[0].normal = {0, 1, 0};
        f.bodies[0].velocity[1] -= 9.81f * f.dt;
        c.step(f);
    }
    check(c.result().impacts == 0, "Gravity is removed from contact velocity deltas");
    f.grounded = true;
    for (auto& b : f.bodies) b.velocity = {};
    c.step(f);
    const auto points = c.result().impact_points;
    for (int i = 0; i < 80; ++i) c.step(f);
    check(c.result().impact_points == points, "Resting on a surface never accumulates impact points");
}
void recovery_and_timeout() {
    Challenge c;
    auto f = skater(); c.begin(f); fall(f,c,20); hit(f,c,0);
    f.bailed = false; c.step(f);
    check(c.result().phase == Phase::results && !c.result().cancelled, "Native recovery completes a bail");
    c.begin(f);
    for (int i = 0; i < 3100; ++i) c.step(f);
    check(c.result().cancelled, "An attempt that never bails times out");
    c.begin(f); f.bailed = true; f.grounded = false;
    for (auto& b : f.bodies) { b.contact = false; b.velocity = {0,-1,0}; }
    for (int i = 0; i < 800; ++i) c.step(f);
    check(c.result().phase == Phase::results, "A ragdoll that never settles still ends its round");
    c.begin(skater()); c.cancel("No Bail was enabled.");
    check(c.result().cancelled && c.result().detail == "No Bail was enabled.", "Feature conflicts preserve a cancellation reason");
}
void native_regressions() {
    // Actual header captured from the supported live local skater, before
    // relocation of pointers. +1c is 395; +20 is the 0.03333-second timestep.
    const unsigned char captured[]{
        0x50,0xe5,0x56,0xe9,0x01,0,0,0, 0x20,0x48,0x57,0xe9,0x01,0,0,0,
        0x01,0,0x01,0,0,0,0,0, 0,0,0,0,0x8b,0x01,0,0, 0x88,0x88,0x08,0x3d};
    const auto header = decode_physics_pose_header(captured);
    check(header && header->count == 395 && header->model == 0x1e956e550 && header->local == 0x1e9574820,
        "The live pose count is decoded independently of the float timestep");
    check(!decode_physics_pose_header(std::span(captured).first(16)), "A partial native header is rejected");
    auto corrupt = std::to_array(captured);
    corrupt[0x1c] = 0;
    check(!decode_physics_pose_header(corrupt), "Unknown skeleton counts fail closed");
    const std::array<int,24> live_map{380,102,101,278,277,276,275,49,48,47,46,45,44,43,42,344,343,342,341,11,10,9,8,7};
    const std::array<Region,24> expected{
        Region::torso,Region::head,Region::head,Region::left_arm,Region::left_arm,Region::left_arm,Region::left_arm,
        Region::right_arm,Region::right_arm,Region::right_arm,Region::right_arm,Region::torso,Region::torso,Region::torso,Region::torso,
        Region::left_leg,Region::left_leg,Region::left_leg,Region::left_leg,Region::right_leg,Region::right_leg,Region::right_leg,Region::right_leg,Region::torso};
    for (std::size_t i=0; i<live_map.size(); ++i)
        check(region_for_joint(live_map[i]) == expected[i], "The captured body map preserves left/right anatomy");
    check(!region_for_joint(10000), "An unknown joint cannot receive a guessed injury region");
    check(injury_joint_for_body(102)==103 && injury_joint_for_body(101)==101 &&
        injury_joint_for_mesh(103)==103 && injury_joint_for_mesh(102)==101 && injury_joint_for_mesh(101)==101,
        "The native Neck1 head proxy damages the skull, while both neck mesh joints follow the neck proxy");
    check(!injury_joint_for_body(380) && !injury_joint_for_body(10000) && !injury_joint_for_mesh(0),
        "Dummy and unknown joints cannot be assigned an anatomical injury");
    std::array<float,16> actor{0,0,-1,0, 0,1,0,0, 1,0,0,0, -56,1,-18,1};
    check(model_to_world(actor, {1,2,3}) == Vec3{-53,3,-19}, "Projected bones include actor rotation and placement");
    const auto before = model_to_world(actor, {0,-.1f,0});
    actor[13] -= 10;
    const auto after = model_to_world(actor, {0,-.1f,0});
    check(std::abs((before[1]-after[1])-10) < .001f, "Fall distance follows actor movement even when the model pose stays unchanged");
}
void camera_timing() {
    // The game rotates/translates the same camera after publishing the client
    // snapshot. Projecting a world pose must use the later draw-time matrix.
    alignas(8) std::array<unsigned char, 0xb0> camera{};
    constexpr std::uintptr_t base = 0x140000000;
    auto type = base + dingosdk::game::build::v20260929::engine::camera_vtable;
    std::array<float,16> final{0,0,-1,0, 0,1,0,0, 1,0,0,0, -56,1,-18,1};
    float fov = 65;
    std::memcpy(camera.data(), &type, sizeof(type));
    std::memcpy(camera.data()+0x50, final.data(), sizeof(final));
    std::memcpy(camera.data()+0xac, &fov, sizeof(fov));
    dingosdk::GameView view{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}, 40,
        reinterpret_cast<std::uintptr_t>(camera.data())};
    check(dingosdk::refresh_game_view(base, view) && view.world == final && view.vertical_fov == fov,
        "Draw-time camera refresh replaces a stale client rotation, position and FOV");
    final[3] = final[7] = final[11] = final[15] = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(camera.data()+0x50, final.data(), sizeof(final));
    check(dingosdk::refresh_game_view(base, view) && view.world[12]==-56 && view.world[14]==-18,
        "SIMD metadata cannot intermittently hide an otherwise valid camera pose");
    const auto saved = view;
    fov = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(camera.data()+0xac, &fov, sizeof(fov));
    check(!dingosdk::refresh_game_view(base, view) && std::memcmp(view.world.data(), saved.world.data(), sizeof(view.world)) == 0 && view.vertical_fov == saved.vertical_fov,
        "An incomplete or invalid camera update cannot partially replace a valid view");
    fov = 65;
    std::memcpy(camera.data()+0xac, &fov, sizeof(fov));
    type = base + dingosdk::game::build::v20260929::client_source_spawn::free_camera_vtable;
    std::memcpy(camera.data(), &type, sizeof(type));
    check(dingosdk::refresh_game_view(base, view), "The verified free camera also supports live projection");
    type = base + 1;
    std::memcpy(camera.data(), &type, sizeof(type));
    check(!dingosdk::refresh_game_view(base, view), "A recycled camera object of another type is rejected");
}
void rendered_pose() {
    const float half = std::sqrt(.5f);
    std::array<float,12> trajectory{1,1,1,0, 0,half,0,half, -56,1,-18,0};
    const auto root = append_pose(identity_pose, trajectory);
    check(root.has_value(), "The final animation trajectory composes a world transform");
    if (!root) return;
    std::array<float,12> child{1,1,1,0, 0,0,0,1, 1,2,3,0};
    child[3] = child[11] = std::numeric_limits<float>::quiet_NaN();
    const auto composed = append_pose(*root, child);
    check(composed && std::abs((*composed)[12]+53) < .001f && std::abs((*composed)[13]-3) < .001f &&
        std::abs((*composed)[14]+19) < .001f,
        "Render bones follow trajectory rotation and world placement once, ignoring metadata lanes");
    trajectory[0]=2; trajectory[1]=3; trajectory[2]=4;
    const auto scaled_root = append_pose(identity_pose, trajectory);
    const auto scaled_child = scaled_root ? append_pose(*scaled_root, child) : std::nullopt;
    check(scaled_child && std::abs((*scaled_child)[12]+44) < .001f && std::abs((*scaled_child)[13]-7) < .001f &&
        std::abs((*scaled_child)[14]+20) < .001f, "Parent scale follows its rotated axes, including nonuniform scale");
    child[7]=0;
    check(!append_pose(*root, child), "An uninitialized rotation cannot publish a render pose");
    std::array<PoseMatrix,26> world{};
    for (std::size_t i=0; i<world.size(); ++i) {
        const auto parent = render_parent_indices[i];
        check(parent < static_cast<int>(i), "The captured animation hierarchy composes parents before children");
        std::array<float,12> bone{1,1,1,0, 0,0,0,1, 0,.1f,0,0};
        if (i==0) bone[9]=0;
        if (i==1) { bone[8]=-56; bone[9]=1; bone[10]=-18; }
        const auto at = append_pose(parent < 0 ? identity_pose : world[static_cast<std::size_t>(parent)], bone);
        if (at) world[i]=*at;
    }
    check(std::abs(world[9][12]+56)<.001f && std::abs(world[9][13]-1.8f)<.001f && std::abs(world[9][14]+18)<.001f,
        "The rendered head chain lands in world space without the actor transform applied again");
    for (const auto index : overlay_joint_indices)
        check(region_for_joint(render_joint_ids[static_cast<std::size_t>(index)]).has_value(), "Every rendered limb has a named injury region");
    check(render_joint_ids[static_cast<std::size_t>(overlay_joint_indices[0])]==103 && overlay_parents[0]==1,
        "The overlay starts at the actual Head joint rather than a physics-only dummy marker");
}
void mesh_skinning() {
    SkeletonMesh mesh;
    mesh.rig.parents.fill(-1);
    mesh.rig.inverse_bind.fill(identity_pose);
    mesh.required[0]=mesh.required[1]=true;
    mesh.rig.parents[1]=0;
    mesh.rig.inverse_bind[1][13]=-2;
    std::array<std::array<float,12>,render_bone_count> local{};
    local[0]={1,1,1,0, 0,0,0,1, 30,4,-9,0};
    local[1]={1,1,1,0, 0,0,0,1, 0,2,0,0};
    MeshPose pose;
    check(skin_render_pose(mesh,local,pose),"Only the used mesh bones require initialized poses");
    const auto vertex=model_to_world(pose.skin[1],{.1f,2,.2f});
    check(std::abs(vertex[0]-30.1f)<.001f && std::abs(vertex[1]-6)<.001f && std::abs(vertex[2]+8.8f)<.001f,
        "Inverse bind cancels bind offset and applies world placement once");
    const float half=std::sqrt(.5f);
    local[1][6]=half; local[1][7]=half;
    check(skin_render_pose(mesh,local,pose),"Articulated bone rotation supports mesh skinning");
    const auto rotated=model_to_world(pose.skin[1],{1,2,0});
    check(std::abs(rotated[0]-30)<.001f && std::abs(rotated[1]-7)<.001f,
        "Vertices rotate around the bone bind origin, rather than the model origin");
    const auto saved=pose;
    local[1][7]=0;
    check(!skin_render_pose(mesh,local,pose) && pose.skin==saved.skin,"Invalid poses cannot partially replace a rendered mesh pose");
    local[1][7]=half; mesh.required[0]=false;
    check(!skin_render_pose(mesh,local,pose),"A required bone cannot use a missing parent");
    bool rejected{};
    try { (void)read_mesh_geometry(std::array<std::byte,200>{}); } catch (const std::exception&) {rejected=true;}
    check(rejected,"A truncated or changed mesh header fails safely");
}
void rendered_world_skinning() {
    SkeletonMesh mesh; mesh.rig.inverse_bind.fill(identity_pose);
    mesh.required[0]=mesh.required[1]=true; mesh.rig.parents[1]=0;
    mesh.rig.inverse_bind[1][13]=-2;
    std::array<PoseMatrix,render_bone_count> world{};
    world[0]=world[1]=identity_pose;
    world[0][12]=world[1][12]=10;
    auto placement=identity_pose; placement[12]=30; placement[14]=-9;
    MeshPose pose;
    check(place_render_skin(mesh,world,identity_pose,pose),"A native world-space skin needs no entity transform");
    const auto unplaced=model_to_world(pose.skin[1],{0,2,0});
    check(std::abs(unplaced[0]-10)<.001f && std::abs(unplaced[1]-2)<.001f,
        "Already positioned native skin preserves its world coordinates and bind cancellation");
    check(place_render_skin(mesh,world,placement,pose),"Composed renderer matrices can be used without composing their hierarchy again");
    const auto vertex=model_to_world(pose.skin[1],{0,2,0});
    check(std::abs(vertex[0]-40)<.001f && std::abs(vertex[1]-2)<.001f && std::abs(vertex[2]+9)<.001f,
        "Only a separately extracted root translation is restored to native skin matrices");
    placement={0,0,-1,0, 0,1,0,0, 1,0,0,0, 30,0,-9,1};
    check(place_render_skin(mesh,world,placement,pose),"Final native placement supports turning the skater");
    const auto turned=model_to_world(pose.skin[1],{0,2,0});
    check(std::abs(turned[0]-30)<.001f && std::abs(turned[2]+19)<.001f,"Final placement rotates and translates the selected pose together");
    world[1][12]=11;
    check(place_render_skin(mesh,world,identity_pose,pose) && std::abs(pose.skin[1][12]-11)<.001f,
        "The current composed pose replaces the previous position without a packed-buffer delay");
    for (const auto lane : {3u,7u,11u,15u}) world[1][lane]=std::numeric_limits<float>::quiet_NaN();
    check(place_render_skin(mesh,world,identity_pose,pose) && std::abs(pose.skin[1][12]-11)<.001f && pose.skin[1][15]==1,
        "Native matrix metadata is ignored and the GPU receives a clean affine matrix");
    const auto saved=pose.skin; world[1][12]=std::numeric_limits<float>::quiet_NaN();
    check(!place_render_skin(mesh,world,placement,pose) && pose.skin==saved,"An incomplete rendered pose cannot partially replace the last valid pose");
    std::array<std::array<float,12>,render_bone_count> packed{};
    packed[0]={1,0,0,10, 0,1,0,0, 0,0,1,0};
    packed[1]={0,0,1,10, 0,1,0,0, -1,0,0,0};
    check(place_packed_render_skin(mesh,packed,identity_pose,pose),"A completed native draw packet can be decoded directly");
    const auto packet_vertex=model_to_world(pose.skin[1],{1,2,3});
    check(std::abs(packet_vertex[0]-13)<.001f && std::abs(packet_vertex[1]-2)<.001f && std::abs(packet_vertex[2]+1)<.001f,
        "Packed draw skin preserves world placement, handedness and bind cancellation");
    auto root=identity_pose; root[12]=30; root[14]=-9;
    check(place_packed_render_skin(mesh,packed,root,pose) && std::abs(pose.skin[1][12]-40)<.001f,
        "A draw packet's separately extracted root is restored once");
    const auto packed_saved=pose.skin; packed[1][3]=std::numeric_limits<float>::quiet_NaN();
    check(!place_packed_render_skin(mesh,packed,root,pose) && pose.skin==packed_saved,
        "An invalid packed draw packet cannot partially replace a valid pose");
}
void raster_camera_input() {
    std::array<std::byte,320> input{};
    auto world=identity_pose;
    world[12]=-17; world[13]=2; world[14]=45;
    for (const auto lane : {3u,7u,11u,15u}) world[lane]=std::numeric_limits<float>::quiet_NaN();
    std::memcpy(input.data()+0x40,world.data(),sizeof(world));
    const float fov=65*3.14159265358979323846f/180;
    std::memcpy(input.data()+0x104,&fov,sizeof(fov));
    RenderCamera camera;
    check(decode_render_camera(input,camera) && std::abs(camera.vertical_fov-65)<.001f &&
        camera.world[12]==-17 && camera.world[13]==2 && camera.world[14]==45 && camera.world[15]==1 && camera.world[3]==0,
        "Raster camera keeps native placement and FOV while cleaning metadata lanes");
    const auto saved=camera;
    auto moved=input;
    RenderCamera impact;
    check(offset_render_camera(moved,{.02f,-.01f,-.025f},.008f,.94f,impact) &&
        std::abs(impact.vertical_fov-61.1f)<.001f && std::abs(impact.world[12]+16.98f)<.001f,
        "A bounded render-view copy supplies the same impacted camera to scene and skeleton");
    for (std::size_t i=0;i<input.size();++i) {
        const bool xyz=i>=0x40 && i<0x80 && (i-0x40)%16<12;
        const bool fov_byte=i>=0x104 && i<0x108;
        check(xyz || fov_byte || moved[i]==input[i],"Camera response preserves native metadata and all unrelated render-view fields");
    }
    const auto unchanged=moved;
    check(!offset_render_camera(moved,{1,0,0},.008f,.94f,impact) && moved==unchanged,
        "An out-of-bounds response cannot partially modify the view copy");
    check(!offset_render_camera(moved,{},std::numeric_limits<float>::quiet_NaN(),.94f,impact) && moved==unchanged,
        "A non-finite camera response is rejected before any modification");
    auto focused=input;
    const Vec3 player{-15,2,35};
    check(offset_render_camera(focused,{},0,.7f,impact,&player,1) && impact.world[8]<0 &&
        std::abs(impact.vertical_fov-45.5f)<.001f && impact.world[12]==-17,
        "Slow-motion zoom turns toward the skater along native negative Z without moving through scenery");
    const Vec3 far_side{-7,2,44};
    focused=input;
    check(offset_render_camera(focused,{},0,.6f,impact,&far_side,1) && impact.world[10]>=std::cos(.349066f)-.001f,
        "Player framing is capped at twenty degrees even for a large lateral offset");
    const Vec3 behind{-17,2,55};
    focused=input;
    check(offset_render_camera(focused,{},0,.7f,impact,&behind,1) && impact.world[8]==0 && impact.world[10]==1,
        "A player behind the camera cannot flip the submitted view");
    const auto before_bad_focus=focused;
    const Vec3 invalid_target{std::numeric_limits<float>::infinity(),0,0};
    check(!offset_render_camera(focused,{},0,.7f,impact,&invalid_target,1) && focused==before_bad_focus &&
        !offset_render_camera(focused,{},0,.5f,impact,&player,1) && focused==before_bad_focus,
        "Invalid targets and excessive zoom cannot partially change render inputs");
    for (std::size_t i=0;i<input.size();++i) {
        const bool xyz=i>=0x40 && i<0x80 && (i-0x40)%16<12;
        const bool fov_byte=i>=0x104 && i<0x108;
        check(xyz || fov_byte || focused[i]==input[i],"Player framing preserves native SIMD metadata and unrelated render fields");
    }
    world[4]=1; // Individually unit-length axes must also be orthogonal.
    world[5]=0;
    std::memcpy(input.data()+0x40,world.data(),sizeof(world));
    check(!decode_render_camera(input,camera) && camera.world==saved.world && camera.vertical_fov==saved.vertical_fov,
        "A torn or unrelated raster basis cannot replace the valid camera");
    check(!decode_render_camera(std::span(input).first(319),camera),"Truncated native render views are rejected");
    world=identity_pose; world[12]=std::numeric_limits<float>::infinity();
    std::memcpy(input.data()+0x40,world.data(),sizeof(world));
    check(!decode_render_camera(input,camera),"Non-finite raster placement cannot reach the GPU");
    world[12]=0;
    std::memcpy(input.data()+0x40,world.data(),sizeof(world));
    const float invalid=0;
    std::memcpy(input.data()+0x104,&invalid,sizeof(invalid));
    check(!decode_render_camera(input,camera),"Invalid raster FOV cannot replace a valid camera");
}
void varied_fracture_planes() {
    const auto first=varied_fracture_location(277,1000);
    check(first==varied_fracture_location(277,1000),"A fracture keeps its chosen location across frames and repeated contacts");
    check(first!=varied_fracture_location(277,2000) && first!=varied_fracture_location(102,1000),
        "Another fall or another bone can choose a different fracture location and angle");
    float smallest=1,largest=0;
    for (unsigned i=0;i<256;++i) {
        const auto location=varied_fracture_location(i,1000+i*51);
        check(location.fraction>=.1f && location.fraction<=.9f && std::abs(location.tilt_a)<=.18f && std::abs(location.tilt_b)<=.18f,
            "Random fractures stay inside the bone and use bounded cut angles");
        smallest=std::min(smallest,location.fraction); largest=std::max(largest,location.fraction);
    }
    check(smallest<.2f && largest>.8f,"Fracture variation reaches both ends of the usable bone length rather than staying near its midpoint");
    auto inverse=identity_pose;
    inverse[0]=0; inverse[2]=-1; inverse[8]=1; inverse[10]=0; inverse[12]=-2; inverse[13]=3; inverse[14]=1;
    const Vec3 point{4,1,2};
    const auto local=model_to_world(inverse,point);
    const Vec3 low{local[0]-2*first.fraction,local[1]-.1f,local[2]-.05f};
    const Vec3 high{local[0]+2*(1-first.fraction),local[1]+.1f,local[2]+.05f};
    const auto plane=make_fracture_plane(inverse,low,high,first);
    check(plane && std::abs((*plane)[0]*point[0]+(*plane)[1]*point[1]+(*plane)[2]*point[2]+(*plane)[3])<.00001f,
        "The randomized cut passes through the selected bone location after rotated bind-space conversion");
    check(!make_fracture_plane(inverse,{}, {},first),"Degenerate bone bounds cannot create a clipping plane");
}
void render_root_timing() {
    MeshPose native{}; native.skin.fill(identity_pose);
    native.skin[1][12]=10; native.skin[103][12]=10.2f; native.skin[103][13]=2;
    native.skin[103][0]=0; native.skin[103][2]=-1; native.skin[103][8]=1; native.skin[103][10]=0;
    native.at_ms=123; native.sequence=456; native.native_draw=native.render_export=true;
    MeshPose shown;
    check(rebase_render_root(native,{10.26f,0,0},shown),"A measured render-root delay can follow the current owned actor");
    check(std::abs(shown.skin[1][12]-10.26f)<.001f && std::abs(shown.skin[103][12]-10.46f)<.001f && shown.skin[103][13]==2,
        "Root synchronization preserves the native head offset and articulation");
    check(shown.skin[103][2]==-1 && shown.skin[103][8]==1 && shown.at_ms==123 && shown.sequence==456 && native.skin[1][12]==10,
        "Root synchronization leaves rotation, capture age and the immutable source unchanged");
    native.skin[1][13]=1.1321067f;
    const auto head_height=native.skin[103][13];
    for (const auto collider_height : {1.1321067f,1.3321068f,1.1321067f}) {
        check(rebase_render_root(native,{10.26f,collider_height,.15f},shown),"Measured collider height changes retain a valid pose");
        check(shown.skin[1][13]==native.skin[1][13] && shown.skin[103][13]==head_height &&
            std::abs(shown.skin[1][12]-10.26f)<.001f && std::abs(shown.skin[1][14]-.15f)<.001f,
            "A 20 cm collider step cannot lift the skeleton, while horizontal correction stays active");
    }
    const auto saved=shown.skin;
    check(!rebase_render_root(native,{100,0,0},shown) && shown.skin==saved,"A teleport cannot stretch an old skeleton onto a new location");
    check(!rebase_render_root(native,{std::numeric_limits<float>::quiet_NaN(),0,0},shown) && shown.skin==saved,
        "Unreadable current placement cannot partially replace a valid displayed pose");
    check(!rebase_render_root(native,{10.26f,100,0},shown) && shown.skin==saved,
        "Preserving animation height does not hide a vertical teleport mismatch");
}
void mesh_streams() {
    // Small synthetic MeshSet: separate vertex streams, a nonidentity bone
    // palette and eight influences, without bundling copyrighted game assets.
    std::vector<std::byte> resource(4096),geometry(570);
    const auto put=[]<class T>(std::vector<std::byte>& bytes,std::size_t offset,T value) {
        std::memcpy(bytes.data()+offset,&value,sizeof(value));
    };
    put(resource,0,std::uint32_t{208}); put(resource,4,std::uint32_t{188});
    put(resource,8,std::uint32_t{384}); put(resource,124,std::uint8_t{1});
    put(resource,180,std::uint16_t{1}); put(resource,48,std::uint64_t{208});
    constexpr std::size_t lod=224,sections=512,palette=2600;
    put(resource,lod,std::uint32_t{1}); put(resource,lod+8,std::uint32_t{5});
    put(resource,lod+12,std::uint64_t{sections-16}); put(resource,lod+84,std::uint32_t{33});
    put(resource,lod+88,std::uint32_t{30}); put(resource,lod+92,std::uint32_t{540});
    put(resource,lod+116,std::uint8_t{1});
    put(resource,palette,std::uint16_t{7}); put(resource,palette+2,std::uint16_t{103});
    const unsigned strides[]{12,8,8,4,4},formats[]{3,23,23,13,13};
    for (unsigned s=0;s<5;++s) {
        const auto section=sections+s*384,decl=section+112;
        put(resource,section+16,std::uint64_t{palette-16}); put(resource,section+24,std::uint16_t{2});
        put(resource,section+31,std::uint8_t{3}); put(resource,section+32,std::uint32_t{1});
        put(resource,section+36,std::uint32_t{s*3}); put(resource,section+40,std::uint32_t{s*108});
        put(resource,section+44,std::uint32_t{3});
        put(resource,decl+96,std::uint8_t{5}); put(resource,decl+97,std::uint8_t{5});
        for (unsigned a=0;a<5;++a) {
            put(resource,decl+a*4,static_cast<std::uint8_t>(a+1));
            put(resource,decl+a*4+1,static_cast<std::uint8_t>(formats[a]));
            put(resource,decl+a*4+3,static_cast<std::uint8_t>(a));
            put(resource,decl+64+a*2,static_cast<std::uint8_t>(strides[a]));
        }
        for (unsigned v=0;v<3;++v) {
            const auto base=s*108;
            put(geometry,base+v*12,Vec3{v==1 ? 1.f : 0.f,v==2 ? 1.f : 0.f,0});
            put(geometry,base+36+v*8,static_cast<std::uint16_t>(v==1 ? 1 : 0));
            // A zero-weight slot may legitimately contain an unused index.
            put(geometry,base+36+v*8+2,std::uint16_t{65535});
            put(geometry,base+60+v*8,std::uint16_t{1});
            put(geometry,base+84+v*4,static_cast<std::uint8_t>(v==2 ? 128 : 255));
            put(geometry,base+96+v*4,static_cast<std::uint8_t>(v==2 ? 127 : 0));
            put(geometry,540+(s*3+v)*2,static_cast<std::uint16_t>(v));
        }
    }
    SkeletonRig rig;
    rig.parents.fill(0); rig.parents[0]=-1; rig.inverse_bind.fill(identity_pose);
    rig.regions.fill(Region::torso); rig.regions[103]=Region::head;
    const auto mesh=read_skinned_mesh(resource,geometry,rig);
    check(mesh.vertices.size()==15 && mesh.indices.size()==15,"All five body sections decode with their own stream bases");
    check(mesh.vertices[1].bones[0]==103 && mesh.vertices[1].region==static_cast<unsigned>(Region::head),
        "The section bone palette maps skinning indices to player bones and injury regions");
    check(mesh.vertices[1].part==103 && mesh.vertices[0].part==7 && mesh.rig.injury_parts[101]==101 &&
        mesh.rig.injury_parts[102]==101,
        "Skull and neck mesh surfaces have distinct anatomical injury zones, independent of physics attachment IDs");
    check(mesh.vertices[2].weights[0]==128 && mesh.vertices[2].weights[4]==127 && mesh.vertices[2].bones[4]==103,
        "Both sets of four bone weights decode from separate streams");
    check(mesh.indices[3]==3 && mesh.indices.back()==14 && mesh.required[0] && mesh.required[7] && mesh.required[103],
        "Section indices are rebased and skinning ancestors are required");
    check(std::abs(std::abs(mesh.vertices[0].normal[2])-1)<.001f,"Triangle normals support the 3D X-ray material");
    const auto fails=[&](const char* message) {
        bool rejected{};
        try {(void)read_skinned_mesh(resource,geometry,rig);} catch (const std::exception&) {rejected=true;}
        check(rejected,message);
    };
    put(geometry,84,std::uint8_t{254}); fails("Unnormalized weights fail instead of distorting the mesh");
    put(geometry,84,std::uint8_t{255}); put(geometry,36,std::uint16_t{2});
    fails("An active skinning index cannot exceed its palette");
    put(geometry,36,std::uint16_t{0}); put(geometry,540,std::uint16_t{3});
    fails("Triangle vertices cannot cross section boundaries");
    put(geometry,540,std::uint16_t{0}); put(resource,sections+16,std::uint64_t{0});
    fails("A missing bone palette fails before reading geometry");
}
void individual_bone_hits() {
    Challenge c; auto f=skater();
    f.bodies[0].region=Region::left_arm; f.bodies[0].joint=277;
    f.bodies[1].region=Region::left_arm; f.bodies[1].joint=278;
    check(c.begin(f),"A challenge accepts mapped native body joints");
    fall(f,c,10); hit(f,c,0);
    const auto forearm=c.result().bone_injuries[277];
    check(forearm.severity>0 && c.result().bone_injuries[278].severity==0,
        "A forearm contact cannot mark an untouched hand as impacted");
    f.bodies[1].contact=true; f.bodies[1].normal={0,1,0}; f.bodies[1].velocity={}; c.step(f);
    check(c.result().bone_injuries[278].severity>0 && c.result().impacts==1,
        "A hand hit within the forearm's region cooldown updates its injury without another scored impact");
    f.bodies[0].contact=f.bodies[1].contact=false;
    for (int i=0;i<15;++i) c.step(f);
    fall(f,c,20); hit(f,c,1);
    check(c.result().bone_injuries[277].severity==forearm.severity && c.result().bone_injuries[278].fractured,
        "A later hand impact preserves the forearm injury and fractures only the hand collider");
    check(c.begin(skater()) && c.result().bone_injuries[277].severity==0 && c.result().bone_injuries[278].severity==0,
        "Retry clears every tracked bone injury");
    f=skater(); f.bodies[0].joint=static_cast<int>(injury_bone_count);
    check(!c.begin(f),"Out-of-range native bone mappings cannot index injury storage");
}
void rapid_head_hits_and_contact_episodes() {
    Challenge c; auto f=skater();
    f.bodies[0].joint=101; f.bodies[0].injury_joint=*injury_joint_for_body(101);
    f.bodies[1].joint=102; f.bodies[1].injury_joint=*injury_joint_for_body(102);
    f.bodies[1].region=Region::head;
    c.begin(f); fall(f,c,20); hit(f,c,0);
    const auto points=c.result().impact_points;
    const auto chain=c.result().current_chain;
    hit(f,c,1);
    check(c.result().bone_injuries[101].fractured && c.result().bone_injuries[103].fractured &&
        c.result().bone_injuries[102].severity==0,
        "A neck-then-head collision within 20ms fractures both distinct anatomical zones");
    check(c.result().impacts==1 && c.result().impact_points==points && c.result().current_chain==chain &&
        c.result().fractures==1,
        "Injuries during score cooldown cannot duplicate region points, chains or fracture bonuses");
    const auto skull=c.result().bone_injuries[103].severity;
    f.grounded=false;
    for (int i=0;i<20;++i) {
        f.bodies[1].velocity=i%2 ? Vec3{} : Vec3{0,-20,0};
        c.step(f);
    }
    check(c.result().bone_injuries[103].severity==skull && c.result().impacts==1,
        "A held contact cannot repeat injury or score when solver/animation velocities change after cooldown");
    f.bodies[1].normal_valid=false; f.bodies[1].velocity={0,-20,0}; c.step(f);
    f.bodies[1].normal_valid=true; f.bodies[1].velocity={}; c.step(f);
    check(c.result().bone_injuries[103].severity==skull,
        "Missing detailed normals cannot release and rearm a raw held contact");
    fall(f,c,20); hit(f,c,1);
    check(c.result().bone_injuries[103].severity>skull && c.result().impacts==2,
        "A genuinely separated and renewed head contact starts another injury episode");

    c.begin(skater()); f=skater();
    f.bodies[0].joint=101; f.bodies[1].joint=102; f.bodies[1].region=Region::head;
    f.bodies[1].injury_joint=103; c.begin(f); fall(f,c,20);
    f.bailed=true; f.bodies[0].contact=f.bodies[1].contact=true;
    f.bodies[0].normal=f.bodies[1].normal={0,1,0};
    f.bodies[0].velocity=f.bodies[1].velocity={}; c.step(f);
    check(c.result().impacts==1 && c.result().bone_injuries[101].severity>0 &&
        c.result().bone_injuries[103].severity>0,
        "Simultaneous head and neck contacts injure both zones while scoring the region once");

    f.bodies[0].contact=f.bodies[1].contact=false; c.step(f);
    const auto before_gap=c.result().bone_injuries[103].severity;
    f.valid=false; c.step(f); f.valid=true; f.bodies[1].contact=true;
    f.bodies[1].velocity={0,-20,0}; c.step(f); f.bodies[1].velocity={}; c.step(f);
    const auto after_gap=c.result().bone_injuries[103].severity;
    check(after_gap==before_gap && c.result().impacts==1,
        "A contact already present on rebaseline cannot become a delayed scored hit");
    f=skater(); f.bodies[0].injury_joint=static_cast<int>(injury_bone_count);
    check(!c.begin(f),"Out-of-range anatomical mappings cannot index injury storage");
}
void contact_record_selection() {
    std::array<unsigned char,0x70> record{};
    const auto write=[&](std::size_t offset,const auto& value) {
        std::memcpy(record.data()+offset,&value,sizeof(value));
    };
    write(0,Vec3{1,0,0}); write(0x10,Vec3{0,1,0});
    write(0x40,Vec3{10,20,30}); write(0x50,1.f); write(0x54,20.f);
    const auto second=decode_contact_detail(record);
    check(second && second->normal==Vec3{0,1,0} && second->speed==20 && second->point==Vec3{10,20,30},
        "A stronger second native contact direction is not discarded because the first normal is valid");
    check(second && !second->point_valid,"Two native directions cannot associate the shared point with the strongest normal");
    write(0x50,30.f);
    check(decode_contact_detail(record)->normal==Vec3{1,0,0} && decode_contact_detail(record)->grounded,
        "A stronger wall contact retains its speed without hiding a simultaneous supporting ground normal");
    write(0,Vec3{std::numeric_limits<float>::quiet_NaN(),0,0});
    check(decode_contact_detail(record)->normal==Vec3{0,1,0},"An invalid normal cannot hide another valid native contact");
    write(0x54,std::numeric_limits<float>::infinity());
    check(!decode_contact_detail(record) && !decode_contact_detail(std::span(record).first(0x60)),
        "Invalid speeds and partial native records cannot invent usable contact detail");
    record={};
    check(!decode_contact_detail(record),"A native-cleared record has no contact normal or position");
    write(0,Vec3{0,1,0}); write(0x50,10.f); write(0x40,Vec3{.3f,0,0});
    check(decode_contact_detail(record)->point_valid,"A single contact direction has an attributable world contact point");
    write(0x40,Vec3{std::numeric_limits<float>::quiet_NaN(),0,0});
    check(decode_contact_detail(record) && !decode_contact_detail(record)->point_valid,
        "Malformed contact position disables rotation evidence while retaining usable linear contact detail");
}
void rotational_contacts() {
    const auto pose=[](float angle) {
        const float sine=std::sin(angle),cosine=std::cos(angle);
        return std::array<float,16>{cosine,sine,0,0,-sine,cosine,0,0,0,0,1,0,0,0,0,1};
    };
    for (int scenario=0;scenario<9;++scenario) {
        Challenge c; auto f=skater(); f.body_count=1; f.center={};
        auto& body=f.bodies[0]; body.joint=102; body.injury_joint=103;
        body.pose=pose(0); body.pose_valid=true;
        c.begin(f); f.bailed=true; c.step(f);
        body.pose=pose(-.4f);
        if (scenario==8) body.velocity={0,-20,0};
        c.step(f);
        if (scenario==6) {f.valid=false; c.step(f); f.valid=true;}
        body.contact=scenario!=1; body.normal={0,1,0};
        body.contact_point={.3f,0,0}; body.point_valid=true;
        body.velocity={};
        if (scenario==2) body.pose=pose(-.8f); // Constant spin: no stopping impact.
        if (scenario==3) body.point_valid=false;
        if (scenario==4 || scenario==8) body.pose_valid=false;
        if (scenario==5) body.pose=pose(2.f); // Reject an ambiguous large turn.
        if (scenario==7) body.contact_point={10,0,0}; // Wrong/remote point.
        c.step(f);
        if (scenario==0)
            check(c.result().impacts==1 && c.result().bone_injuries[103].severity>0,
                "A contact-confirmed rotational stop injures the skull even with zero center linear velocity");
        else if (scenario==8)
            check(c.result().impacts==1,"Missing optional pose detail preserves ordinary linear impact detection");
        else
            check(c.result().impacts==0 && c.result().bone_injuries[103].severity==0,
                "Constant spin, no contact, missing point/pose, large turns, telemetry gaps and remote points cannot invent rotational injury");
    }
    Challenge c; auto f=skater(); f.body_count=1;
    auto& body=f.bodies[0]; body.pose=pose(0); body.pose_valid=true;
    c.begin(f); f.bailed=true; f.grounded=true; c.step(f);
    for (int i=0;i<55;++i) {body.pose=pose(i%2 ? 0.f : .2f); c.step(f);}
    check(c.result().phase==Phase::bailed,"A still-spinning ragdoll cannot settle solely because its linear speed is low");
}
void impacts_before_bail() {
    for (int scenario=0;scenario<8;++scenario) {
        Challenge c; auto f=skater();
        f.bodies[0].joint=102; f.bodies[0].injury_joint=103;
        f.bodies[1].joint=343; f.bodies[1].region=Region::left_leg;
        c.begin(f); fall(f,c,12);
        for (std::size_t i=0;i<f.body_count;++i) {
            auto& body=f.bodies[i];body.velocity={};body.contact=true;body.normal={0,1,0};
        }
        // The collision resolves while the animation still reports upright.
        c.step(f);
        check(c.result().phase==Phase::attempt && c.result().impacts==0 &&
            c.result().bone_injuries[103].severity==0 && c.result().bone_injuries[343].severity==0,
            "An upright impact retains evidence without awarding damage or score");
        if (scenario==1) f.manual_bail=true;
        if (scenario==2) {for (int i=0;i<11;++i)c.step(f);} // Older than 200ms.
        if (scenario==3) {f.valid=false;c.step(f);f.valid=true;}
        if (scenario==4) {++f.entity;}
        if (scenario==5) {f.center[0]+=100;}
        if (scenario==6) {f.bodies[0].joint=101;f.bodies[0].injury_joint=101;}
        if (scenario==7) {f.bodies[0].contact=f.bodies[1].contact=false;c.step(f);}
        f.bailed=true;c.step(f);
        if (scenario==0 || scenario==7) {
            check(c.result().bone_injuries[103].severity>0 && c.result().bone_injuries[343].severity>0 &&
                c.result().impacts==2,"A delayed natural bail retains simultaneous skull and foot hits after they have already slowed or separated");
            const auto head=c.result().bone_injuries[103].severity;
            for (int i=0;i<15;++i)c.step(f);
            check(c.result().bone_injuries[103].severity==head && c.result().impacts==2,
                "Committing a retained impact cannot repeat it during a resting contact");
        } else if (scenario==6) {
            check(c.result().bone_injuries[101].severity==0 && c.result().bone_injuries[103].severity==0 &&
                c.result().bone_injuries[343].severity>0,"Changing one body's identity rejects its history without losing another body's valid impact");
        } else {
            check(c.result().bone_injuries[103].severity==0 && c.result().bone_injuries[343].severity==0 &&
                c.result().impacts==0,"Manual bails, stale history, telemetry gaps, ownership changes and teleports cannot revive prior upright damage");
        }
    }
    Challenge c;auto f=skater();f.body_count=1;f.bodies[0].joint=102;f.bodies[0].injury_joint=103;
    c.begin(f);
    auto& head=f.bodies[0];head.contact=true;head.normal={0,1,0};head.velocity={};
    head.contact_speed=18;head.speed_valid=true;c.step(f);
    f.bailed=true;c.step(f);
    check(c.result().bone_injuries[103].fractured && c.result().impacts==1,
        "A native-recorded contact speed retains the skull hit when animation has already erased the linear delta");
    const auto severity=c.result().bone_injuries[103].severity;
    for(int i=0;i<20;++i)c.step(f);
    check(c.result().bone_injuries[103].severity==severity,"A held native contact speed cannot duplicate an injury");
    c.begin(skater());f=skater();f.bodies[0].contact_speed=18;f.bodies[0].speed_valid=true;f.bailed=true;c.step(f);
    check(c.result().impacts==0,"Native speed without a confirmed contact never scores");
}
void board_landing_load() {
    for (unsigned scenario=0;scenario<13;++scenario) {
        Challenge c; auto f=skater(); f.body_count=2; f.dt=.033f;
        f.bodies[0].joint=10; f.bodies[0].injury_joint=10; f.bodies[0].region=Region::right_leg;
        f.bodies[1].joint=343; f.bodies[1].injury_joint=343; f.bodies[1].region=Region::left_leg;
        for (auto& body:f.bodies) body.velocity={};
        // Captured high-drop trace: owned ridden board, incoming ~20m/s,
        // cause 6 ~20m/s and an upward support normal, with no foot contacts.
        f.board.identity=100; f.board.riding=scenario!=3; f.board.velocity={-2.067f,-20.0f,-6.950f};
        if (scenario==12) f.board.hard_landing=true;
        if (scenario==12) {f.board.speed=20;f.board.normal={0,1,0};}
        c.begin(f);
        if (scenario==8) {auto gap=f;gap.valid=false;c.step(gap);}
        if (scenario==6) f.board.velocity={-2,-20,-7}; else f.board.velocity={-1.961f,0,-7.152f};
        f.board.hard_landing=scenario!=4; f.board.normal=scenario==5 ? Vec3{0,0,1} : Vec3{0,1,0}; f.board.speed=20;
        if (scenario==7) f.board.identity=101;
        if (scenario==10) f.center[0]=100;
        f.bailed=scenario!=1 && scenario!=2 && scenario!=11;
        f.manual_bail=scenario==9;
        c.step(f);
        if (scenario==1 || scenario==2 || scenario==11) {
            check(c.result().impacts==0 && c.result().bone_injuries[10].severity==0,
                "A hard board landing cannot damage feet until it causes a bail");
            if (scenario==2) for (int i=0;i<8;++i)c.step(f);
            if (scenario==11) f.board.identity=101;
            f.bailed=true;c.step(f);
        }
        const bool accepted=scenario<=1;
        check((c.result().bone_injuries[10].severity>0)==accepted &&
            (c.result().bone_injuries[343].severity>0)==accepted,
            "Feet receive only recent native-confirmed landings from a continuously owned ridden board");
        if (accepted) {
            check(c.result().bone_injuries[10].severity>199 && c.result().bone_injuries[10].severity<201 &&
                c.result().bone_injuries[343].severity==c.result().bone_injuries[10].severity && c.result().impacts==2,
                "The captured landing shares severity across both feet and their two scoring regions");
            const auto severity=c.result().bone_injuries[10].severity;
            for(int i=0;i<8;++i)c.step(f);
            check(c.result().bone_injuries[10].severity==severity && c.result().impacts==2,
                "A held native landing flag cannot repeat foot damage");
        }
    }
}
void pose_publication_order() {
    PosePublication clock;
    const auto early=clock.issue(),final=clock.issue();
    check(clock.publish(final,true) && !clock.publish(early,false),"A delayed evaluation worker cannot overwrite a later render export");
    const auto next_evaluation=clock.issue();
    check(!clock.publish(next_evaluation,false),"An early update for the next frame leaves the current pending render pose intact");
    const auto later_export=clock.issue();
    check(clock.publish(later_export,true),"A later final export can replace a pending export before presentation");
    clock.presented();
    const auto fallback=clock.issue();
    check(clock.publish(fallback,false),"Walking and culled skaters can use animation after the exported pose has been presented");
    const auto old_owner=clock.issue();
    clock.invalidate();
    check(!clock.publish(old_owner,true),"Respawn or map change rejects unfinished work from the previous pose owner");
    const auto new_owner=clock.issue();
    check(clock.publish(new_owner,false) && !clock.pending_export,"The new owner starts with no inherited render barrier");
}
void challenge_progression() {
    for (unsigned i=0;i<static_cast<unsigned>(ChallengeKind::count);++i) {
        Config config; config.kind=static_cast<ChallengeKind>(i); config.target=default_target(config.kind);
        check(valid_config(config) && decode_config(encode_config(config))==std::optional(config),
            "Every challenge has a valid default and exact saved configuration round trip");
        Challenge c; auto f=skater();
        config.target=config.kind==ChallengeKind::free_play ? 5000.f : 1.f;
        check(c.begin(f,config),"Each challenge accepts its own target");
        const auto pinned=config;
        config.scoring.impact_rate=0;
        check(c.result().config==pinned,"Changing menu configuration cannot alter an attempt in progress");
        fall(f,c,20); f.center[1]-=1.2f; hit(f,c,0);
        f.grounded=false;
        for (auto& b : f.bodies) { b.contact=false; b.velocity={0,-1,0}; }
        for (int j=0;j<60;++j) c.step(f);
        f.grounded=true; f.bodies[1].velocity={2,0,0}; c.step(f);
        for (int j=0;j<30;++j) { f.center[0]+=.04f; c.step(f); }
        f.bailed=false; c.step(f);
        const auto& result=c.result();
        check(result.phase==Phase::results && !result.cancelled,"Each challenge completes through native recovery");
        check(result.target_met==(pinned.kind!=ChallengeKind::free_play),
            "Targets use their corresponding score, chain, fracture, fall, air or slide metric");
        check(result.points==result.impact_points+result.chain_points+result.fracture_points+
            result.fall_points+result.airtime_points+result.slide_points,"Score explanations sum exactly to the final total");
    }
    Challenge c; auto f=skater(); c.begin(f); fall(f,c,20); hit(f,c,0);
    const auto first_points=c.result().impact_points;
    fall(f,c,20); hit(f,c,1);
    check(c.result().best_chain==2 && c.result().chain_points>0 && c.result().impact_points>first_points,
        "Distinct contacted regions extend a chain and add its configured bonus");
    f.dt=.3f; c.step(f);
    check(c.result().current_chain==0 && c.result().chain_remaining_s==0 && c.result().best_chain==2,
        "Missing telemetry ends the current chain while retaining its completed best");
    f.dt=.02f;
    for (int i=0;i<20;++i) { f.bodies[0].contact=f.bodies[1].contact=false; c.step(f); }
    fall(f,c,20); hit(f,c,0);
    check(c.result().current_chain==1,"The first later contact begins a fresh chain after the gap");
    Config invalid; invalid.kind=ChallengeKind::fractures; invalid.target=7;
    check(!valid_config(invalid) && !c.begin(skater(),invalid),"An impossible seventh fractured region cannot be a target");
    check(!decode_config("{}") && !decode_config(std::string(4097,' ')),"Malformed and oversized saved configuration is rejected");
}
void saved_personal_bests() {
    Config config; config.kind=ChallengeKind::score; config.target=100;
    Challenge c; auto f=skater(); c.begin(f,config); fall(f,c,20); hit(f,c,0);
    PersonalBest best;
    check(!best.record(c.result()) && best.attempts==0,"An unfinished attempt cannot enter personal bests");
    f.bailed=false; c.step(f);
    check(best.record(c.result()) && best.attempts==1 && best.successes==1 && best.score==c.result().points,
        "Completed targets update personal bests and success counts");
    constexpr std::string_view map="levels/test/slam";
    const auto document=encode_personal_best(map,config,best);
    check(decode_personal_best(document,map,config)==std::optional(best),"Completed personal bests survive a saved-state round trip");
    auto higher_bruise=config; higher_bruise.scoring.bruise_threshold=100;
    check(personal_best_key(map,config)!=personal_best_key(map,higher_bruise) &&
        !decode_personal_best(document,map,higher_bruise),"Changing injury rules cannot mix personal-best records");
    auto different=config; different.scoring.chain_step=.5f;
    check(personal_best_key(map,config)!=personal_best_key(map,different) &&
        personal_best_key(map,config)!=personal_best_key("levels/test/other",config),"Maps and scoring rules use separate personal bests");
    check(!decode_personal_best(document,"levels/test/other",config) && !decode_personal_best(document,map,different),
        "Full saved identity prevents a key collision from mixing personal bests");
    auto lower=c.result(); lower.points=1; lower.target_progress=1; lower.target_met=false;
    check(!best.record(lower) && best.attempts==2 && best.successes==1 && best.score==c.result().points,
        "A lower completed score counts its attempt without replacing the best");
    lower.cancelled=true;
    const auto saved=best;
    check(!best.record(lower) && best==saved,"Cancelled attempts cannot change any personal-best field");
    check(!decode_personal_best("{}",map,config) && !decode_personal_best(std::string(4097,' '),map,config),
        "Malformed and oversized best records are rejected");
}
void saved_damage_thresholds() {
    const Config defaults;
    constexpr std::string_view legacy=R"({"version":1,"kind":0,"target":5000,"rules":[10,500,100,50,25,0.25,1.5,160,225]})";
    check(decode_config(legacy)==std::optional(defaults) && encode_config(defaults).find("bruiseThreshold")==std::string::npos,
        "Older scoring saves retain any-damage bruises and their original personal-best identity");
    Config configured; configured.scoring.bruise_threshold=120;
    configured.scoring.head_fracture=500; configured.scoring.limb_fracture=800;
    check(decode_config(encode_config(configured))==std::optional(configured),
        "Bruise, head fracture and other fracture thresholds survive saved configuration");
    const auto encoded=encode_config(configured);
    for (const auto bad_value : {"true","-1","10001","600"}) {
        auto bad=encoded;
        const auto at=bad.find("\"bruiseThreshold\":120");
        bad.replace(at,std::string_view("\"bruiseThreshold\":120").size(),std::string("\"bruiseThreshold\":")+bad_value);
        check(!decode_config(bad),"Invalid or above-fracture bruise thresholds are rejected");
    }
    configured.scoring.bruise_threshold=std::numeric_limits<float>::quiet_NaN();
    check(!valid_config(configured),"A non-finite bruise threshold cannot reach injury classification");
}
}
void personal_best_transactions() {
    using namespace dingosdk::slam;
    std::map<std::string,std::string,std::less<>> profile;
    unsigned reads{},writes{}; bool can_save=false;
    const BestBook::Read read=[&](std::string_view key)->std::optional<std::string> {
        ++reads; const auto found=profile.find(key);
        return found==profile.end() ? std::nullopt : std::optional(found->second);
    };
    const BestBook::Write write=[&](std::string_view key,std::string_view document) {
        ++writes; if (!can_save) return false;
        profile[std::string(key)]=document; return true;
    };
    constexpr std::string_view map="levels/test/transactions";
    Config config; config.kind=ChallengeKind::score; config.target=500;
    profile[personal_best_key(map,config)]=encode_personal_best(map,config,{400,400,3,1});
    Result result; result.config=config; result.phase=Phase::results;
    result.points=600; result.target_progress=600; result.target_met=true;
    BestBook book;
    const auto recorded=book.record(1,map,result,read,write);
    check(recorded && recorded->improved && !recorded->saved && recorded->best.score==600 && recorded->best.attempts==4 && recorded->best.successes==2,
        "A completed result merges its stored best once and survives a failed profile write");
    const auto count=writes;
    check(!book.record(1,map,result,read,write) && writes==count && book.lookup(map,config,read).best.attempts==4,
        "Repeated publication of the same attempt cannot write or count it twice");
    result.cancelled=true;
    check(!book.record(2,map,result,read,write) && writes==count,"Cancelled results never enter the best store");
    check(!book.flush(write) && book.lookup(map,config,read).best.attempts==4,"Failed save retries do not count more attempts");
    can_save=true;
    check(book.flush(write) && book.lookup(map,config,read).saved,"A later successful write clears the unsaved state");
    BestBook restored;
    check(restored.lookup(map,config,read).best==recorded->best,"A new session restores the completed best and counters from profile text");
    result.cancelled=false; result.points=1; result.target_progress=1; result.target_met=false;
    const auto lower=book.record(2,map,result,read,write);
    check(lower && !lower->improved && lower->saved && lower->best.score==600 && lower->best.attempts==5 && lower->best.successes==2,
        "A lower completed result persists its attempt count without replacing the record");
    auto different=config; different.target=700;
    check(book.lookup(map,different,read).best.attempts==0 && book.lookup("levels/test/elsewhere",config,read).best.attempts==0,
        "Changing maps or targets selects a separate personal best");
    result.config=different;
    check(book.record(3,map,result,read,write)->best.attempts==1 && book.lookup(map,config,read).best.attempts==5,
        "A new challenge's completion does not alter the previous challenge's counters");
    const auto reads_before=reads;
    (void)book.lookup(map,config,read);
    check(reads==reads_before,"Recently recorded and unsaved bests use the session cache");
}
int main() {
    lifecycle(); contact_scoring(); same_region(); invalidation(); free_fall_and_rest(); recovery_and_timeout(); native_regressions(); camera_timing(); rendered_pose(); mesh_skinning(); rendered_world_skinning(); raster_camera_input(); render_root_timing(); mesh_streams(); individual_bone_hits(); pose_publication_order(); challenge_progression(); saved_personal_bests();
    personal_best_transactions();
    saved_damage_thresholds();
    varied_fracture_planes(); rapid_head_hits_and_contact_episodes(); contact_record_selection(); rotational_contacts(); impacts_before_bail(); board_landing_load();
    if (failures) return 1;
    std::cout << "Slam Challenge scoring and lifecycle checks passed.\n";
}
