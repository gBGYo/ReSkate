#include "Extension/Blood/blood_native.h"
#include "Extension/Blood/blood_options.h"
#include "Extension/Blood/blood_surface.h"
#include "Engine/Game/Settings/transient_count_lease.h"
#ifdef _WIN32
#include "Extension/Decals/native_decals.h"
#endif
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>
#include <memory>
#include <unordered_map>

namespace {
using namespace dingosdk::blood;
// Models contain large fixed scenes. Keep them on the heap so tests exercising
// several snapshots do not depend on the executable's default stack reserve.
int failures{};
void check(bool ok,const char* message) {if (!ok) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}}
Frame frame() {
    Frame f; f.entity=11; f.world=22; f.valid=true; f.dt=.02f; f.body_count=2; f.center={0,5,0};
    for (unsigned i=0;i<2;++i) {
        auto& b=f.bodies[i]; b.joint=100+i; b.injury_joint=100+i; b.normal={0,1,0};
        b.pose={1,0,0,0,0,1,0,0,0,0,1,0,0,5,0,1}; b.pose_valid=true;
    }
    return f;
}
ImpactContact hit(const Frame& f,unsigned i=0) {
    ImpactContact h; h.body=f.bodies[i]; h.body.contact=true; h.body.contact_point={.2f,5,0}; h.body.point_valid=true;
    h.incoming_velocity={2,-12,0}; h.episode=i+1; h.body_index=i; h.speed=12; h.severity=144; h.new_episode=true;
    return h;
}
std::size_t count(const BloodScene& s) {std::size_t n{}; for (const auto& x:s.sources) n+=x.id!=0; return n;}
void physical_contacts() {
    auto f=frame(); ImpactTracker c; check(c.begin(f),"ImpactTracker starts");
    for (auto& b:f.bodies) b.velocity={0,-20,0}; c.step(f);
    f.bailed=true;
    for (auto& b:f.bodies) {b.contact=true; b.velocity={}; b.point_valid=true; b.contact_point={1,2,3};}
    c.step(f);
    check(c.impact_contacts().size()==2,"Two same-region body hits survive score aggregation");
    const auto first=c.impact_contacts()[0];
    check(first.new_episode && first.episode && first.body.contact_point==Vec3{1,2,3} && first.incoming_velocity[1]==-20,
        "Contact feed retains physical geometry, incoming motion and episode identity");
    c.step(f); check(c.impact_contacts().empty(),"Held solver contact does not publish another hit");
    c.reset(); check(c.impact_contacts().empty(),"Cancellation clears the frame contact batch");
    c.reset(); f=frame(); c.begin(f);
    f.bodies[0].velocity={0,-20,0}; c.step(f);
    f.bodies[0].contact=true; f.bodies[0].velocity={}; f.bodies[0].contact_point={7,8,9}; f.bodies[0].point_valid=true;
    c.step(f); check(c.impact_contacts().empty(),"Pre-bail impact is deferred");
    f.bodies[0].contact_point={50,50,50}; f.bailed=true; c.step(f);
    check(c.impact_contacts().size()==1 && c.impact_contacts()[0].body.contact_point==Vec3{7,8,9},
        "Deferred pre-bail impact uses its original location");
    c.reset(); f=frame(); c.begin(f); f.bodies[0].velocity={0,-20,0}; c.step(f);
    f.bailed=true; f.bodies[0].contact=true; f.bodies[0].velocity={}; f.bodies[0].hit.board=true; c.step(f);
    check(c.impact_contacts().empty(),"Board-only contact never produces blood geometry");
}
void tracker_lifecycle() {
    ImpactTracker tracker;
    auto f=frame(); f.body_count=1;
    check(tracker.begin(f),"Standalone tracker arms during ordinary play");
    for (unsigned i=0;i<4000;++i) tracker.step(f);
    f.bailed=true; f.bodies[0].contact=true; f.bodies[0].speed_valid=true; f.bodies[0].contact_speed=10;
    tracker.step(f);
    check(tracker.impact_contacts().size()==1,"Blood still detects a fall after more than a minute of skating");
    const auto first=tracker.impact_contacts()[0].episode;
    tracker.step(f);
    check(tracker.impact_contacts().empty(),"Held native contact emits once");
    f.bodies[0].contact_speed=20; tracker.step(f);
    check(tracker.impact_contacts().size()==1 && !tracker.impact_contacts()[0].new_episode &&
        tracker.impact_contacts()[0].episode==first,"A harder peak retains its contact episode");
    f.bodies[0].contact_speed=0;
    for (unsigned i=0;i<11;++i) tracker.step(f);
    f.bodies[0].contact_speed=10; tracker.step(f);
    check(tracker.impact_contacts().size()==1 && tracker.impact_contacts()[0].new_episode &&
        tracker.impact_contacts()[0].episode>first,"Quiet native evidence rearms a fresh hit while still touching");
    const auto second=tracker.impact_contacts()[0].episode;
    f.bailed=false; f.bodies[0].contact=false; tracker.step(f);
    f.bailed=true; f.bodies[0].contact=true; tracker.step(f);
    check(tracker.impact_contacts().size()==1 && tracker.impact_contacts()[0].episode>second,
        "Recovery automatically rearms the independent tracker with unique episode IDs");
    f.valid=false; tracker.step(f);
    f.valid=true; f.bodies[0].contact_speed=50; tracker.step(f);
    check(tracker.impact_contacts().empty(),"Missing telemetry never bridges into a phantom hit");
    f.entity=99; tracker.step(f);
    check(!tracker.running() && tracker.impact_contacts().empty(),"Changing local owner clears the tracker");
    f=frame(); f.body_count=1; tracker.begin(f);
    f.center[0]=100; tracker.step(f);
    check(!tracker.running(),"Teleport discontinuities reset the detector");
    f=frame(); f.body_count=1; tracker.begin(f);
    f.bailed=true; tracker.step(f);
    for (unsigned i=0;i<1000;++i) tracker.step(f);
    f.bodies[0].contact=true; f.bodies[0].speed_valid=true; f.bodies[0].contact_speed=10;
    tracker.step(f);
    check(tracker.impact_contacts().size()==1,"A long ragdoll fall is not cut off by a challenge timer");
}
void sources() {
    auto storage=std::make_unique<BloodModel>(); auto& model=*storage; auto f=frame(); f.bailed=true; auto h=hit(f); h.body.normal={1,0,0}; h.incoming_velocity={}; const BloodOptions options{true,false,1};
    model.step(f,{&h,1},options,1);
    auto s=model.scene(); check(count(s)==3 && s.accepted==1,"One hit starts sparse spray, droplets and a connected wound stream");
    check(s.sources[2].transform[4]>0 && s.sources[2].transform[5]==0 && s.sources[2].transform[6]==0,
        "Gush launches away from the hurt surface");
    const auto id=s.sources[0].id;
    const auto old_x=s.sources[1].transform[12];
    f.bodies[0].pose[12]=.3f; model.step(f,{&h,1},options,1);
    check(count(model.scene())==3 && model.scene().accepted==1,"Repeated delivery cannot spawn the same episode twice");
    check(std::abs(model.scene().sources[1].transform[12]-old_x-.3f)<.001f && model.scene().sources[0].transform[12]==s.sources[0].transform[12],
        "Attached sources follow the wound while the impact spray stays in world space");
    f.bodies[0].pose={0,1,0,0,-1,0,0,0,0,0,1,0,.3f,5,0,1};
    model.step(f,{},options,1);
    check(std::abs(model.scene().sources[2].transform[12]-.3f)<.001f &&
        std::abs(model.scene().sources[2].transform[13]-5.215f)<.001f && model.scene().sources[2].transform[4]==0,
        "Tumbling rotates both the wound's attachment and its launch direction");
    h.new_episode=false; h.episode=50; model.step(f,{&h,1},options,1);
    check(model.scene().accepted==1,"Episode peak upgrades do not trigger fresh spray");
    for (unsigned i=0;i<38;++i) model.step(f,{},options,1);
    check(count(model.scene())==2 && !model.scene().sources[2].emitting,"Brief gush stops before its fragments drain");
    for (unsigned i=0;i<65;++i) model.step(f,{},options,1);
    check(count(model.scene())==0,"All sources expire using bounded simulation lifetimes");
    h=hit(f); h.episode=100; model.step(f,{&h,1},options,1);
    check(model.scene().sources[0].id>id,"Expired handles are never reused");
    f.bailed=false; model.step(f,{},options,1);
    check(!model.scene().sources[1].emitting,"Recovery stops attached bleeding");
    ++f.world; model.step(f,{},options,1); check(count(model.scene())==0,"World changes discard all sources");
    f.bailed=true; h=hit(f); model.step(f,{&h,1},options,1);
    model.step(f,{},options,2); check(count(model.scene())==0,"Loading generation cancels old effects");
    model.step(f,{&h,1},{true,true,1},2); check(count(model.scene())==1,"Reduced effects retain only a small impact spray");
    model.step(f,{}, {true,false,0},2); check(count(model.scene())==0,"Zero amount disables blood");
    h=hit(f); h.body.point_valid=false; h.body.pose_valid=false;
    model.step(f,{&h,1},options,3); check(count(model.scene())==0,"Missing world geometry never produces a guessed splatter");
    h=hit(f); h.body.contact_point[0]=std::numeric_limits<float>::quiet_NaN(); h.body.pose_valid=false;
    model.step(f,{&h,1},options,4); check(count(model.scene())==0,"Nonfinite geometry is rejected");
    model.clear(); h=hit(f); model.step(f,{&h,1},options,5);
    f.bodies[0].pose[12]+=10; model.step(f,{},options,5);
    check(!model.scene().sources[1].emitting,"Teleport-sized body jumps cannot draw a trail across the map");
}
void budgets_and_time() {
    auto storage=std::make_unique<BloodModel>(); auto& model=*storage; auto f=frame(); f.bailed=true; f.body_count=max_bodies;
    std::array<ImpactContact,max_bodies> hits{};
    for (unsigned i=0;i<max_bodies;++i) {f.bodies[i]=f.bodies[0]; f.bodies[i].joint=100+i; hits[i]=hit(f,i);}
    for (unsigned n=0;n<20;++n) {for (auto& h:hits) h.episode+=max_bodies; model.step(f,hits,{true,false,1},1);}
    check(count(model.scene())<=max_blood_sources && model.scene().dropped>0,"Contact storms obey source and per-step budgets");
    unsigned wounds{};
    for (const auto& s:model.scene().sources) wounds+=s.id && s.kind==BloodKind::trail && s.emitting;
    check(wounds<=2,"Contact storms cannot start more than two simultaneous wound streams");
    model.clear(); auto h=hit(f); model.step(f,{&h,1},{true,false,1},1);
    const auto before=model.scene().sources[1].age; f.dt=.005f; model.step(f,{}, {true,false,1},1);
    check(std::abs(model.scene().sources[1].age-before-.005f)<.0001f,"Slow motion uses the actual simulation delta");
    model.clear(); h.severity=25; f.dt=.02f; model.step(f,{&h,1},{true,false,.7f},1);
    const auto& matrix=model.scene().sources[1].transform;
    check(std::sqrt(matrix[4]*matrix[4]+matrix[5]*matrix[5]+matrix[6]*matrix[6])>.9f,
        "Ordinary accepted impacts retain a visible drop scale at the default amount");
}
struct Fake {
    unsigned created{},moved{},stopped{},killed{},released{}; bool fail{};
    std::vector<unsigned> assets;
    BloodNativeFunctions functions() {return {this,
        [](void* c,BloodKind kind,BloodIntensity intensity,const std::array<float,16>&)->std::uintptr_t {
            auto& f=*static_cast<Fake*>(c); f.assets.push_back(blood_asset_index(kind,intensity));
            ++f.created; return f.fail ? 0 : f.created;},
        [](void* c,std::uintptr_t&,const std::array<float,16>&) {++static_cast<Fake*>(c)->moved;},
        [](void* c,std::uintptr_t&,bool kill) {auto& f=*static_cast<Fake*>(c); ++f.stopped; f.killed+=kill;},
        [](void* c,std::uintptr_t& h) {++static_cast<Fake*>(c)->released; h=0;}};}
};
void native_lifetimes() {
    auto storage=std::make_unique<BloodModel>(); auto& model=*storage; auto f=frame(); f.bailed=true; auto h=hit(f); model.step(f,{&h,1},{true,false,1},1);
    auto s=model.scene(); Fake fake; BloodNativePool pool; const auto functions=fake.functions();
    pool.update(s,functions,true); check(fake.created==3 && pool.active()==3,"Native pool takes one owned handle per source");
    pool.update(s,functions,true); check(fake.created==3 && fake.moved==2,"Moving sources update without respawning");
    for (auto& x:s.sources) x.emitting=false;
    pool.update(s,functions,true); pool.update(s,functions,true);
    check(fake.stopped==3 && !fake.killed && !fake.released,"Emission stops once and permits existing particles to drain");
    s.sources={}; pool.update(s,functions,true);
    check(fake.released==3 && pool.active()==0,"Expired native sources release exactly once");
    s=model.scene(); pool.update(s,functions,true); ++s.generation; s.sources={}; pool.update(s,functions,true);
    check(fake.released==6 && pool.active()==0,"World generation change releases the previous scene");
    s=model.scene(); pool.update(s,functions,true); pool.update(s,functions,false); pool.clear(functions);
    check(fake.released==9,"Disable and repeated cleanup do not leak or double release");
    fake.fail=true; pool.update(s,functions,true); const auto attempts=fake.created; pool.update(s,functions,true);
    check(fake.created==attempts && pool.active()==0,"Failed creation is not retried every frame");
    pool.clear(functions); for (auto& x:s.sources) x.emitting=false; pool.update(s,functions,true);
    check(fake.created==attempts,"Delayed contacts are not replayed after the emission window");
}

std::size_t marks(const BloodGroundScene& s) {
    return std::count_if(s.marks.begin(),s.marks.end(),[](const auto& mark){return mark.id!=0;});
}
Frame dragging() {
    auto f=frame(); f.bailed=true;
    for (auto& b:f.bodies) {
        b.region=Region::left_arm; b.contact=true; b.point_valid=true; b.hit.world=true;
        b.contact_point={0,0,0}; b.pose[13]=.15f; b.velocity={2,0,0};
    }
    return f;
}
void advance(Frame& f,float distance) {
    f.bodies[0].contact_point[0]+=distance; f.bodies[0].pose[12]+=distance;
}
void impact_strength() {
    auto storage=std::make_unique<BloodModel>(); auto& model=*storage;
    const auto axis_length=[](const auto& t,unsigned axis) {
        return std::sqrt(t[axis]*t[axis]+t[axis+1]*t[axis+1]+t[axis+2]*t[axis+2]);
    };
    float previous_spray{},previous_area{},previous_emission{};
    for (bool reduced:{false,true}) {
        previous_spray=previous_area=previous_emission=0;
        for (float speed:{5.f,12.f,25.f,60.f}) {
            model.clear(); auto f=dragging(); f.body_count=1; auto h=hit(f);
            h.speed=speed; h.severity=speed*speed;
            const BloodOptions options{true,reduced,.7f};
            model.step(f,{&h,1},options,1);
            const auto& scene=model.scene();
            Fake native; BloodNativePool pool; pool.update(scene,native.functions(),true);
            const unsigned asset_offset=speed==5 ? 3 : speed>=18 && !reduced ? 6 : 0;
            check(native.assets.size()==(reduced ? 1 : 3) && native.assets[0]==asset_offset,
                "Native creation selects distinct light, medium and heavy spray assets");
            if (!reduced) check(native.assets[1]==asset_offset+1 && native.assets[2]==asset_offset+2,
                "Attached droplets and streams select the matching emission density");
            const float spray=axis_length(scene.sources[0].transform,4);
            const auto& stain=scene.ground.marks[0].transform;
            const float area=axis_length(stain,0)*axis_length(stain,8);
            check(count(scene)==(reduced ? 1 : 3) && marks(scene.ground)==1,
                "Impact strength respects reduced effects and does not multiply sources or decals");
            check(std::abs(stain[5]-.24f)<.0001f && std::abs(stain[13]-.04f)<.0001f,
                "Larger impact stains preserve projection depth and surface lift");
            if (speed<60) {
                check(spray>previous_spray && area>previous_area,"Small falls, hard slams and extreme slams grow progressively larger");
            } else {
                check(spray==previous_spray && area==previous_area,"Extreme collision speeds cannot grow effects beyond their cap");
            }
            previous_spray=spray; previous_area=area;
            if (!reduced) {
                const auto& drops=scene.sources[1];
                check(axis_length(drops.transform,4)>.9f,"Even a light hit retains visible attached droplets");
                check(speed<60 ? drops.emit_seconds>previous_emission : drops.emit_seconds==previous_emission,
                    "Stronger wounds emit longer, with a bounded maximum");
                previous_emission=drops.emit_seconds;
            }
            const auto source_id=scene.sources[0].id;
            h.severity=3600; h.new_episode=false;
            model.step(f,{&h,1},options,1);
            check(scene.accepted==1 && scene.sources[0].id==source_id && marks(scene.ground)==1,
                "Peak updates do not replay or multiply an existing blood burst");
            for (unsigned i=0;i<200;++i) model.step(f,{},options,1);
            const auto before=marks(scene.ground);
            advance(f,.5f); model.step(f,{},options,1);
            check(speed==5 ? marks(scene.ground)==before : marks(scene.ground)>before,
                "Light wounds stop bleeding sooner while heavy wounds still leave a drag trail");
            for (unsigned i=0;i<450;++i) model.step(f,{},options,1);
            const auto expired=marks(scene.ground);
            advance(f,.5f); model.step(f,{},options,1);
            check(marks(scene.ground)==expired && count(scene)==0,"Even extreme wounds stop emitting and depositing within bounded lifetimes");
        }
    }
    check(blood_intensity(99)==BloodIntensity::light && blood_intensity(100)==BloodIntensity::medium &&
        blood_intensity(323)==BloodIntensity::medium && blood_intensity(324)==BloodIntensity::heavy,
        "Heavy particle bursts now begin at 18 m/s, with light falls below 10 m/s");
    const auto maximum=blood_impact_response(625);
    check(std::abs(maximum.spray_scale-1.9f*1.2f)<.0001f && std::abs(maximum.stain_scale-2.88f*1.2f)<.0001f,
        "Full-size impacts are reached at 25 m/s and their dimensions increase by twenty percent");
    float ordinary_drag_area{};
    for (float speed:{12.f,25.f}) {
        model.clear(); auto f=dragging(); f.body_count=1; auto h=hit(f); h.severity=speed*speed;
        model.step(f,{&h,1},{true,false,.7f},1);
        advance(f,.4f); model.step(f,{}, {true,false,.7f},1);
        const auto& drag=model.scene().ground.marks[1];
        check(drag.id && !drag.impact && drag.kind!=BloodMarkKind::drop,"Strength comparison produces a sliding smear");
        const float area=axis_length(drag.transform,0)*axis_length(drag.transform,8);
        if (speed==12) ordinary_drag_area=area;
        else check(std::abs(area-ordinary_drag_area*4)<.0001f && std::abs(axis_length(drag.transform,4)-.24f)<.0001f,
            "Heavy drag smears cover four times ordinary area while preserving projection depth");
    }
    model.clear(); auto budget_frame=dragging(); budget_frame.body_count=3;
    std::array<ImpactContact,3> ordered_hits;
    for (unsigned i=0;i<3;++i) {
        budget_frame.bodies[i]=budget_frame.bodies[0]; budget_frame.bodies[i].joint=100+i;
        ordered_hits[i]=hit(budget_frame,i); ordered_hits[i].severity=std::array{25.f,144.f,625.f}[i];
    }
    model.step(budget_frame,ordered_hits,{true,false,.7f},1);
    check(model.scene().sources[0].joint==102 && model.scene().sources[3].joint==101 && model.scene().accepted==2,
        "The strongest contacts win the wound budget even when weak contacts appear first");
    // Exercise the contact scoring feed with identical incoming velocities:
    // strength must come from accepted native contact damage.
    for (float speed:{5.f,25.f}) {
        model.clear(); auto f=dragging(); f.bailed=false; f.body_count=1;
        f.bodies[0].contact=false;
        ImpactTracker challenge; check(challenge.begin(f),"Impact strength integration attempt starts");
        challenge.step(f);
        f.bailed=true; f.bodies[0].contact=true;
        f.bodies[0].speed_valid=true; f.bodies[0].contact_speed=speed;
        challenge.step(f);
        model.step(f,challenge.impact_contacts(),{true,false,.7f},1);
        check(model.scene().accepted==1,"Native contact damage reaches blood response");
        const float spray=axis_length(model.scene().sources[0].transform,4);
        if (speed==5) previous_spray=spray;
        else check(spray>previous_spray*2,"A severe native impact creates a substantially larger spray than a light fall");
    }
}
void ground_trails() {
    auto storage=std::make_unique<BloodGroundModel>(); auto& model=*storage; auto f=dragging(); auto h=hit(f);
    model.step(f,{},true,false,1,1);
    check(marks(model.scene())==0,"Uninjured ground contact leaves no blood");
    model.step(f,{&h,1},true,false,1,1);
    check(marks(model.scene())==1,"Injured ground contact leaves an initial stain");
    for (unsigned i=0;i<20;++i) model.step(f,{},true,false,1,1);
    check(marks(model.scene())==1,"A resting body cannot pile up stains each frame");
    advance(f,.4f); model.step(f,{},true,false,1,1);
    check(marks(model.scene())>=2 && marks(model.scene())<=7,"A drag deposits irregular marks within its frame budget");
    const auto first=model.scene().marks[0].transform;
    const auto drag=model.scene().marks[1].transform;
    check(std::abs(drag[4])<.0001f && drag[0]>.07f && std::abs(drag[2])<drag[0]*.4f,
        "Smears follow the slide with bounded angular variation");
    check(drag[13]-drag[5]*.5f<-.05f && drag[13]+drag[5]*.5f>.1f,
        "Projection extends both below the contact and above the ground for grazing views");
    check(model.scene().marks[0].transform==first,"Existing stains remain fixed on the map");
    const auto before=marks(model.scene());
    f.bodies[0].contact=false; advance(f,.4f); model.step(f,{},true,false,1,1);
    check(marks(model.scene())==before,"Airborne motion creates no ground marks");
    f.bodies[0].contact=true; model.step(f,{},true,false,1,1);
    check(marks(model.scene())==before+1,"Recontact starts a new path without bridging airborne travel");
    f.bailed=false; advance(f,.4f); model.step(f,{},true,false,1,1);
    check(marks(model.scene())==before+1,"Recovery preserves stains and stops new deposition");
    f.bailed=true; model.step(f,{},true,false,1,1);
    check(marks(model.scene())==before+1,"A new fall requires a new injury");
    model.step(f,{},true,false,1,2); check(marks(model.scene())==0,"A world generation change clears surface marks");
    model.step(f,{&h,1},true,false,1,2); model.step(f,{},false,false,1,2);
    check(marks(model.scene())==0,"Disabling blood clears surface marks");
    for (unsigned scenario=0;scenario<6;++scenario) {
        model.clear(); f=dragging(); f.body_count=1; h=hit(f);
        if (scenario==0) f.bodies[0].point_valid=false;
        if (scenario==1) f.bodies[0].normal={0,0,0};
        if (scenario==2) {f.bodies[0].hit.world=false; f.bodies[0].hit.board=true;}
        if (scenario==3) f.bodies[0].normal_valid=false;
        if (scenario==4) f.bodies[0].contact_point={5,0,0};
        if (scenario==5) f.bodies[0].normal[1]=std::numeric_limits<float>::quiet_NaN();
        model.step(f,{&h,1},true,false,1,1);
        check(marks(model.scene())==0,"Ambiguous points, invalid normals, boards and remote geometry cannot receive stains");
    }
    model.clear(); f=dragging(); h=hit(f); model.step(f,{&h,1},true,false,1,1);
    advance(f,5); model.step(f,{},true,false,1,1); advance(f,.3f); model.step(f,{},true,false,1,1);
    check(marks(model.scene())==1,"Teleport-sized body movement cancels its drag path");
    model.clear(); f=dragging(); h=hit(f); model.step(f,{&h,1},true,false,1,1);
    advance(f,.3f); f.bodies[0].contact_point[1]=.2f; f.bodies[0].pose[13]+=.2f;
    model.step(f,{},true,false,1,1);
    check(marks(model.scene())==1,"A step between ground planes cannot be bridged by a smear");
    f.bailed=false; f.dt=.1f;
    for (unsigned i=0;i<905;++i) model.step(f,{},true,false,1,1);
    check(marks(model.scene())==0,"Persistent marks have a bounded simulation lifetime");
    model.clear(); f=dragging(); h=hit(f); model.step(f,{&h,1},true,false,1,1);
    f.valid=false; model.step(f,{},true,false,1,1);
    check(marks(model.scene())==1,"A missing physics sample preserves existing stains");
    f.valid=true; advance(f,.3f); model.step(f,{},true,false,1,1);
    check(marks(model.scene())==1,"Resuming after missing telemetry does not bridge a stale drag path");
    auto combined_storage=std::make_unique<BloodModel>(); auto& combined=*combined_storage; f=dragging(); h=hit(f); combined.step(f,{&h,1},{true,false,1},1);
    combined.suspend();
    check(count(combined.scene())==0 && marks(combined.scene().ground)==1,
        "Pausing emission drains particles while retaining the ground scene");
    combined.clear(); check(marks(combined.scene().ground)==0,"Explicit cleanup removes persistent ground marks");
    std::array<std::size_t,2> density{};
    BloodGroundScene low_rate;
    for (const auto region:{Region::torso,Region::left_arm}) {
    for (unsigned test=0;test<2;++test) {
        model.clear(); f=dragging(); f.dt=test ? 1.f/120 : 1.f/30;
        for (auto& body:f.bodies) body.region=region;
        h=hit(f);
        model.step(f,{&h,1},true,false,1,1);
        for (unsigned i=0;i<(test ? 120u : 30u);++i) {advance(f,2*f.dt); model.step(f,{},true,false,1,1);}
        density[test]=marks(model.scene());
        if (!test) low_rate=model.scene();
        else for (std::size_t i=0;i<std::min(density[0],density[1]);++i) {
            const auto& a=low_rate.marks[i]; const auto& b=model.scene().marks[i];
            check(a.kind==b.kind && a.body_variant==b.body_variant,"Sampling rate preserves the selected blood shapes");
            for (unsigned axis=0;axis<16;++axis)
                check(std::abs(a.transform[axis]-b.transform[axis])<.0001f,"Sampling rate preserves blood size, orientation and location");
        }
    }
    check(std::abs(int(density[0])-int(density[1]))<=1,"Ground trail density is independent of physics sampling rate");
    }
    std::array<unsigned,4> variants{};
    float smallest=1,largest=0;
    for (const auto& mark:model.scene().marks) if (mark.id) {
        ++variants[static_cast<unsigned>(mark.kind)];
        const auto& t=mark.transform;
        const float width=std::sqrt(t[8]*t[8]+t[9]*t[9]+t[10]*t[10]);
        check(std::abs(t[13]-.04f)<.0001f && std::abs(t[5]-.24f)<.0001f,
            "Every randomized mark retains the verified ground projection volume");
        check(std::abs(t[14])<.14f,"Random marks stay close to the injured body drag path");
        if (mark.kind==BloodMarkKind::drop) check(width>=.025f*1.15f && width<=.065f*1.15f,"Satellite blood drops remain small");
        else {smallest=std::min(smallest,width); largest=std::max(largest,width);}
    }
    check(std::all_of(variants.begin(),variants.end(),[](auto n){return n>0;}),"A drag mixes three smear silhouettes and separate blood drops");
    check(largest>smallest*1.5f,"Smear widths vary visibly along a drag");
}
void torso_trails() {
    auto storage=std::make_unique<BloodGroundModel>(); auto& model=*storage; auto f=dragging();
    for (unsigned i=0;i<2;++i) {
        auto& b=f.bodies[i]; b.region=Region::torso; b.joint=44+i;
        b.pose[14]=(i ? .12f : -.12f); b.contact_point[2]=b.pose[14]+.12f;
    }
    auto a=hit(f,0),b=hit(f,1); const std::array hits{a,b};
    model.step(f,hits,true,false,1,1);
    check(marks(model.scene())==1,"Multiple injured spine contacts produce one body footprint");
    const auto& first=model.scene().marks[0]; const auto& t=first.transform;
    check(first.kind==BloodMarkKind::body && std::sqrt(t[8]*t[8]+t[10]*t[10])>.35f*1.15f && std::sqrt(t[8]*t[8]+t[10]*t[10])<.65f*1.15f,
        "A torso drag starts with a broad body smear instead of a narrow joint mark");
    check(t[13]-t[5]*.5f<-.1f && t[13]+t[5]*.5f>.35f,
        "Broad torso smears have extra projection headroom for oblique camera views");
    check(std::abs(t[14])<.04f,"The body footprint is centered beneath the torso, not its off-center contact points");
    // Supporting spine proxies can change while the same torso slides.
    f.bodies[0].contact=false;
    for (unsigned i=0;i<12;++i) {
        for (auto& body:f.bodies) {body.pose[12]+=.04f; body.contact_point[0]+=.04f;}
        model.step(f,{},true,false,1,1);
    }
    unsigned broad{};
    for (const auto& mark:model.scene().marks) if (mark.id) {
        broad+=mark.kind==BloodMarkKind::body;
        check(mark.kind==BloodMarkKind::body || mark.kind==BloodMarkKind::drop,
            "Torso support switches retain one broad trail rather than reintroducing spine lines");
    }
    check(broad>=2 && broad<=3,"A sliding torso leaves spaced body scuffs without dominating the smaller limb trails");
    const auto before=marks(model.scene()); f.bodies[1].contact=false;
    model.step(f,{},true,false,1,1);
    check(marks(model.scene())==before,"A torso without verified ground support cannot paint a floating body trail");
    model.clear(); f=dragging();
    for (auto& body:f.bodies) body.region=Region::torso;
    f.bodies[1].contact_point[1]=.3f; f.bodies[1].pose[13]+=.3f;
    a=hit(f); model.step(f,{&a,1},true,false,1,1);
    check(std::abs(model.scene().marks[0].transform[13]-.12f)<.0001f,
        "Torso contacts on separate steps cannot create an averaged floating smear");
    model.clear(); f=dragging(); f.body_count=8;
    std::array<ImpactContact,8> storm{};
    for (unsigned i=0;i<8;++i) {
        f.bodies[i]=f.bodies[0]; f.bodies[i].joint=200+i;
        f.bodies[i].region=i==7 ? Region::torso : Region::left_arm;
        storm[i]=hit(f,i);
    }
    model.step(f,storm,true,false,1,1);
    check(model.scene().marks[0].kind==BloodMarkKind::body && marks(model.scene())<=6,
        "The torso smear has priority over small joints within the existing frame budget");
    model.clear(); f=dragging(); a=hit(f); model.step(f,{&a,1},true,false,1,1);
    check(model.scene().marks[0].kind!=BloodMarkKind::body,"An isolated limb injury still leaves a small localized smear");
}
struct FakeGround {
    unsigned created{},released{},updated{}; bool fail{};
    std::unordered_map<std::uint32_t,float> live;
    std::size_t peak{};
    BloodGroundFunctions functions() {return {this,
        [](void* c,const BloodMark&)->std::uint32_t {
            auto& s=*static_cast<FakeGround*>(c); ++s.created;
            if (s.fail) return 0;
            s.live[s.created]=1; s.peak=std::max(s.peak,s.live.size()); return s.created;
        },
        [](void* c,std::uint32_t& h){auto& s=*static_cast<FakeGround*>(c); ++s.released; check(s.live.erase(h)==1,"Only owned ground handles are released"); h=0;},
        [](void* c,std::uint32_t h,float alpha){
            auto& s=*static_cast<FakeGround*>(c); ++s.updated;
            const auto at=s.live.find(h);
            check(at!=s.live.end() && alpha>=0 && alpha<=1,"Opacity updates target live handles with bounded values");
            if (at!=s.live.end()) {check(alpha<=at->second,"Retiring blood never becomes more opaque again"); at->second=alpha;}
        }};}
};
void ground_lifetimes() {
    auto storage=std::make_unique<BloodGroundModel>(); auto& model=*storage; auto f=dragging(); auto h=hit(f);
    model.step(f,{&h,1},true,false,1,1);
    FakeGround fake; auto pool_storage=std::make_unique<BloodGroundPool>(); auto& pool=*pool_storage; const auto functions=fake.functions();
    pool.update(model.scene(),functions,true); pool.update(model.scene(),functions,true);
    check(fake.created==1 && pool.active()==1,"Native ground pool creates each stationary mark once");
    const auto first_mark=model.scene().marks[0].id;
    for (unsigned i=0;i<250;++i) {
        advance(f,.2f); model.step(f,{},true,false,1,1); pool.update(model.scene(),functions,true);
    }
    check(pool.active()>256 && fake.released==0 && model.scene().marks[0].id==first_mark,
        "A single drag exceeding the old 256-mark cap retains the beginning of its trail");
    for (std::size_t i=0;i<max_blood_marks;++i) {
        advance(f,.2f);
        if (i%100==0) {++h.episode; model.step(f,{&h,1},true,false,1,1);}
        else model.step(f,{},true,false,1,1);
        pool.update(model.scene(),functions,true);
    }
    check(pool.active()>=blood_mark_soft_limit && pool.active()<=max_blood_marks && fake.released>0 && fake.created-fake.released==pool.active(),
        "Long drags retire marks while bounding native ownership");
    check(fake.updated>0 && fake.peak<=max_blood_marks && model.scene().stats.retired>0,
        "Pressure retirement fades marks and never exceeds the native allocation ceiling");
    check(model.scene().marks[0].id==first_mark,
        "An old nearby impact survives pressure while distant lower-value marks retire first");
    pool.update(model.scene(),functions,false); pool.clear(functions);
    check(fake.released==fake.created && pool.active()==0,"Disable and repeated cleanup release every mark exactly once");
    model.clear(); f=dragging(); h=hit(f); model.step(f,{&h,1},true,false,1,2);
    fake.fail=true; pool.update(model.scene(),functions,true); const auto attempts=fake.created;
    pool.update(model.scene(),functions,true);
    check(fake.created==attempts,"Failed decal creation does not loop every tick");
    check(pool.failed()==1,"Failed creation remains observable until the mark leaves the scene");
    pool.clear(functions); auto stale=model.scene(); stale.marks[0].age=2;
    pool.update(stale,functions,true); check(fake.created==attempts,"Late material loading cannot replay stale stains");
    pool.clear(functions); fake.fail=false; pool.update(model.scene(),functions,true);
    auto changed=model.scene(); ++changed.generation; changed.marks={}; pool.update(changed,functions,true);
    check(pool.active()==0,"World replacement releases decals from the previous scene");
}

void ground_density_and_fades() {
    auto storage=std::make_unique<BloodGroundModel>(); auto& model=*storage;
    auto f=dragging(); auto h=hit(f);
    for (unsigned i=0;i<40;++i) {
        // Recontact at one place: old implementation accumulated one native
        // volume per recontact, even when all stains covered the same patch.
        f.bailed=false; model.step(f,{},true,false,1,1);
        f.bailed=true; h=hit(f); h.episode=i+1;
        model.step(f,{&h,1},true,false,1,1);
    }
    check(marks(model.scene())==3 && model.scene().stats.density_rejected==37,
        "Repeated impacts saturate a local patch instead of consuming the global pool");
    f.bailed=false; model.step(f,{},true,false,1,1);
    f.bailed=true; f.bodies[0].contact_point[1]=.2f; f.bodies[0].pose[13]+=.2f;
    h=hit(f); ++h.episode; model.step(f,{&h,1},true,false,1,1);
    check(marks(model.scene())==4,"Density suppression never combines different steps");
    model.clear(); f=dragging(); h=hit(f); model.step(f,{&h,1},true,false,1,1);
    FakeGround fake; auto pool_storage=std::make_unique<BloodGroundPool>(); auto& pool=*pool_storage; const auto functions=fake.functions();
    pool.update(model.scene(),functions,true);
    f.bailed=false; f.dt=.1f;
    for (unsigned i=0;i<860;++i) {model.step(f,{},true,false,1,1); pool.update(model.scene(),functions,true);}
    check(pool.active()==1 && fake.updated>0 && fake.live.begin()->second<.9f && fake.live.begin()->second>.7f,
        "Aging stains fade during their final five simulation seconds without respawning");
    const auto updates=fake.updated;
    pool.update(model.scene(),functions,true);
    check(fake.updated==updates,"An unchanged snapshot does not enqueue another opacity update");
    for (unsigned i=0;i<45;++i) {model.step(f,{},true,false,1,1); pool.update(model.scene(),functions,true);}
    check(pool.active()==0 && fake.created==1 && fake.released==1 && model.scene().stats.expired==1,
        "A faded stain expires once, with a distinct lifetime diagnostic");
}

void ground_burst_pressure() {
    auto storage=std::make_unique<BloodGroundModel>(); auto& model=*storage;
    auto f=dragging(); f.dt=.001f; f.body_count=6;
    std::array<ImpactContact,6> hits;
    // Thousands of fresh independent contacts before any mark is old enough
    // for retirement: reject excess additions, never overwrite fresh marks.
    for (unsigned n=0;n<400;++n) {
        f.bailed=false; model.step(f,{},true,false,1,1);
        f.bailed=true;
        for (unsigned i=0;i<6;++i) {
            auto& b=f.bodies[i]; b=f.bodies[0]; b.joint=100+i;
            b.contact_point={float(n),0,float(i)}; b.pose[12]=float(n); b.pose[14]=float(i);
            hits[i]=hit(f,i); hits[i].episode=n+1;
        }
        model.step(f,hits,true,false,1,1);
    }
    check(marks(model.scene())==max_blood_marks && model.scene().marks[0].id==1 &&
        model.scene().stats.pressure_rejected>0 && model.scene().stats.retirement_started==0,
        "A fresh burst fills the bounded reserve and rejects overflow without erasing impacts");
    check(model.scene().stats.neighbour_checks<2400*20,
        "Spatial queries avoid full-pool scans during a distributed 2400-impact burst");
    f.bailed=false; f.dt=.1f;
    for (unsigned n=0;n<105;++n) model.step(f,{},true,false,1,1);
    f.bailed=true; f.bodies[0].contact_point={1000,0,0}; f.bodies[0].pose[12]=1000;
    auto h=hit(f); model.step(f,{&h,1},true,false,1,1);
    check(marks(model.scene())==max_blood_marks && model.scene().stats.retirement_started==1 && model.scene().stats.retired==0,
        "At capacity, eligible marks begin retirement before any native slot is reused");
    f.bailed=false;
    for (unsigned n=0;n<21;++n) model.step(f,{},true,false,1,1);
    check(marks(model.scene())==max_blood_marks-1 && model.scene().stats.retired==1,
        "Pressure frees a slot only after the full fade interval");
}

void ground_snapshot_replacement() {
    auto scene=std::make_unique<BloodGroundScene>(); scene->entity=11; scene->world=22; scene->generation=1;
    FakeGround fake; auto pool_storage=std::make_unique<BloodGroundPool>(); auto& pool=*pool_storage; const auto functions=fake.functions();
    for (unsigned i=0;i<4;++i) scene->marks[100+i].id=i+1;
    pool.update(*scene,functions,true);
    for (unsigned i=0;i<4;++i) {scene->marks[100+i]={}; scene->marks[i].id=i+5;}
    pool.update(*scene,functions,true);
    check(fake.peak==4 && fake.released==4 && pool.active()==4,
        "A mixed snapshot drains later obsolete slots before allocating earlier new slots");
    pool.clear(functions);
    check(fake.live.empty() && pool.failed()==0,"Cleanup releases all handles and allocation diagnostics");
}

void ground_impact_priority() {
    auto storage=std::make_unique<BloodGroundModel>(); auto& model=*storage;
    auto f=dragging(); f.dt=.001f; f.body_count=6;
    std::array<ImpactContact,6> hits;
    for (unsigned i=0;i<6;++i) {
        auto& b=f.bodies[i]; b=f.bodies[0]; b.joint=100+i;
        b.pose[14]=b.contact_point[2]=float(i)*2; hits[i]=hit(f,i);
    }
    model.step(f,hits,true,false,1,1);
    for (unsigned n=0;n<400;++n) {
        for (auto& b:f.bodies) {b.pose[12]+=.6f; b.contact_point[0]+=.6f;}
        model.step(f,{},true,false,1,1);
    }
    check(marks(model.scene())==max_blood_marks-blood_impact_reserve && model.scene().stats.retirement_started==0,
        "Fresh trails stop at the impact reserve even before retirement is possible");
    const auto first=model.scene().marks[0].id;
    f.body_count=1;
    for (unsigned n=0;n<blood_impact_reserve;++n) {
        f.bailed=false; model.step(f,{},true,false,1,1); f.bailed=true;
        f.bodies[0].pose[12]=f.bodies[0].contact_point[0]=3000+float(n)*2;
        auto h=hit(f); model.step(f,{&h,1},true,false,1,1);
    }
    check(marks(model.scene())==max_blood_marks && model.scene().marks[0].id==first,
        "Impacts consume all reserved slots without overwriting existing blood");

    model.clear(); f=dragging(); f.body_count=1; f.bodies[0].region=Region::torso;
    auto h=hit(f); model.step(f,{&h,1},true,true,1,1);
    f.body_count=4; advance(f,.6f);
    std::array<ImpactContact,3> fresh;
    for (unsigned i=1;i<4;++i) {
        auto& b=f.bodies[i]; b=f.bodies[0]; b.region=Region::left_arm; b.joint=100+i;
        b.pose[14]=b.contact_point[2]=float(i)*3; fresh[i-1]=hit(f,i);
    }
    model.step(f,fresh,true,true,1,1);
    check(marks(model.scene())==3 && model.scene().marks[1].impact && model.scene().marks[2].impact &&
        model.scene().marks[1].kind!=BloodMarkKind::body && model.scene().marks[2].kind!=BloodMarkKind::body,
        "Fresh limb impacts get the frame budget before an existing torso trail");
    model.step(f,{},true,true,1,1);
    check(marks(model.scene())==4 && model.scene().marks[3].impact && model.scene().marks[3].transform[14]>8,
        "An impact deferred by the frame budget is deposited on the next supported sample");

    model.clear(); f=dragging(); f.body_count=1; h=hit(f); model.step(f,{&h,1},true,false,1,1);
    for (unsigned n=0;n<30;++n) {advance(f,n%2 ? -.6f : .6f); model.step(f,{},true,false,1,1);}
    check(model.scene().stats.density_rejected>0,"Repeated dragging saturates its trail patch");
    advance(f,.3f); ++h.episode;
    const auto spawned=model.scene().stats.spawned;
    model.step(f,{&h,1},true,false,1,1);
    check(model.scene().stats.spawned==spawned+1 &&
        std::count_if(model.scene().marks.begin(),model.scene().marks.end(),[](const auto& m){return m.id && m.impact;})==2,
        "A new hit on a bleeding joint places a fresh impact despite dense older trails");
}

void ground_footprints_and_grid() {
    auto storage=std::make_unique<BloodGroundModel>(); auto& model=*storage;
    auto f=dragging(); f.body_count=1;
    auto impact=[&](float x,float z) {
        f.bailed=false; model.step(f,{},true,false,1,1); f.bailed=true;
        f.bodies[0].pose[12]=f.bodies[0].contact_point[0]=x;
        f.bodies[0].pose[14]=f.bodies[0].contact_point[2]=z;
        auto h=hit(f); model.step(f,{&h,1},true,false,1,1);
    };
    for (unsigned scenario=0;scenario<3;++scenario) {
        model.clear(); f=dragging(); f.body_count=1;
        f.bodies[0].region=scenario==0 ? Region::left_arm : Region::torso;
        if (scenario==2) f.bodies[0].velocity={0,0,2};
        for (unsigned i=0;i<3;++i) impact(0,0);
        impact(0,.30f);
        check(marks(model.scene())==(scenario==1 ? 3u : 4u),
            "Overlap follows actual footprint width and orientation instead of a fixed body radius");
    }
    model.clear(); f=dragging(); f.body_count=1;
    impact(0,0);
    const auto offset=model.scene().marks[0].transform[14];
    model.clear();
    for (unsigned i=0;i<3;++i) impact(-.01f,-.01f-offset);
    impact(.01f,.01f-offset);
    check(marks(model.scene())==3,"Neighbours across negative and positive grid boundaries share the density cap");
    f.bailed=false; f.dt=.1f;
    for (unsigned i=0;i<905;++i) model.step(f,{},true,false,1,1);
    impact(.01f,.01f-offset);
    check(marks(model.scene())==1,"Expired grid entries no longer suppress new impacts");

    model.clear(); f=dragging(); f.body_count=1; f.bodies[0].normal={0,.8f,.6f};
    for (unsigned i=0;i<3;++i) impact(0,0);
    f.bodies[0].contact_point[1]=-.015f; f.bodies[0].pose[13]=.135f;
    impact(0,.02f);
    check(marks(model.scene())==3,"Sloped receiver footprints share density across grid cells");
    f.bodies[0].contact_point[1]+=.2f; f.bodies[0].pose[13]+=.2f;
    impact(0,.02f);
    check(marks(model.scene())==4,"Nearby parallel slopes on different levels remain independent");

    // These metre cells hash to the same bucket. Removing the middle, tail,
    // and head must not detach a surviving cell or leave a stale chain.
    model.clear(); f=dragging(); f.body_count=1;
    impact(4096,0); impact(0,0); impact(8192,0);
    check(marks(model.scene())==3,"Hash collisions retain independent distant receivers");
    f.bailed=false; f.dt=.1f;
    for (unsigned i=0;i<500;++i) model.step(f,{},true,false,1,1);
    impact(4096,0);
    f.bailed=false;
    for (unsigned i=0;i<405;++i) model.step(f,{},true,false,1,1);
    check(marks(model.scene())==1,"Expiring colliding cells retains the newer occupant");
    impact(4096,0); impact(4096,0); impact(4096,0);
    check(marks(model.scene())==3,"Collision chain removal preserves density checks for surviving marks");
    model.clear(); impact(4096,0);
    check(marks(model.scene())==1 && model.scene().stats.neighbour_checks==0,"Clear removes all spatial memberships and counters");
}

void draw_budget_ownership() {
    using dingosdk::CountSettingSample;
    dingosdk::TransientCountLease lease;
    CountSettingSample current{11,22,33,120}; unsigned writes{}; bool readable=true,accept=true;
    auto read=[&]() -> std::optional<CountSettingSample> {return readable ? std::optional(current) : std::nullopt;};
    auto write=[&](const CountSettingSample& expected,std::uint32_t value) {
        if (!accept || current!=expected) return false;
        current.value=value; ++writes; return true;
    };
    check(lease.update(2048,read,write) && current.value==2168,"Blood adds room beyond the native environment draw cap");
    check(lease.update(2048,read,write) && writes==1,"Draw allowance neither accumulates nor rewrites unchanged settings");
    check(lease.update(0,read,write) && current.value==120,"Clearing blood restores the original render limit");
    lease.update(2048,read,write); current.value=500;
    check(!lease.update(2048,read,write) && current.value==500,"A player's newer render setting takes precedence");
    check(!lease.update(2048,read,write) && current.value==500,"Active blood does not repeatedly fight an external edit");
    lease.update(0,read,write); lease.update(2048,read,write); ++current.address;
    check(lease.update(0,read,write) && current.value==2548,"A replaced settings object is not overwritten during cleanup");
    current={11,22,33,256}; lease.update(2048,read,write); readable=false;
    check(!lease.update(0,read,write),"Unavailable settings retain restoration for a later tick");
    readable=true;
    check(lease.update(0,read,write) && current.value==256,"Deferred cleanup restores the original total view budget");
    accept=false; check(!lease.update(2048,read,write) && current.value==256,"Rejected typed setters leave the original value intact");
    check(lease.update(0,read,write),"Failed reservation can be cleared without another write");
}
void surface_orientations() {
    auto storage=std::make_unique<BloodGroundModel>(); auto& model=*storage;
    const std::array<Vec3,7> normals{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{.6f,.8f,0}}};
    for (bool vehicle:{false,true}) for (const auto normal:normals) for (bool moving:{false,true}) {
        model.clear(); auto f=dragging(); auto& b=f.bodies[0];
        b.normal=normal; b.hit.vehicle=vehicle;
        for (unsigned i=0;i<3;++i) {
            b.pose[12+i]=b.contact_point[i]+normal[i]*.15f;
            b.velocity[i]=moving ? -normal[i]*4 : 0; // Head-on or stationary: no usable tangent.
        }
        auto h=hit(f); model.step(f,{&h,1},true,false,1,1);
        check(marks(model.scene())==1,"Every valid receiver orientation accepts a fresh impact, including vehicles");
        const auto& m=model.scene().marks[0]; const auto& t=m.transform;
        check(std::all_of(t.begin(),t.end(),[](float x){return std::isfinite(x);}),"Head-on impacts produce finite decal transforms");
        float depth{},length{},width{},xy{},zy{},xz{},lift{};
        for (unsigned i=0;i<3;++i) {
            depth+=t[4+i]*t[4+i]; length+=t[i]*t[i]; width+=t[8+i]*t[8+i];
            xy+=t[i]*t[4+i]; zy+=t[8+i]*t[4+i]; xz+=t[i]*t[8+i];
            lift+=(t[12+i]-b.contact_point[i])*normal[i];
        }
        const bool shallow=vehicle || normal[1]<=.5f;
        check(length>.001f && width>.001f && std::abs(xy)+std::abs(zy)+std::abs(xz)<.0001f,
            "Decal footprint remains nondegenerate and tangent to the contact plane");
        check(std::abs(std::sqrt(depth)-(shallow ? .08f : .24f))<.0001f && std::abs(lift-m.projection_lift)<.0001f,
            "Walls and vehicles receive shallow projection while ground keeps its existing depth");
        const unsigned along=std::abs(normal[1])<.5f ? 1 : 2;
        for (unsigned i=0;i<12;++i) {b.contact_point[along]+=.1f; b.pose[12+along]+=.1f; model.step(f,{},true,false,1,1);}
        check(marks(model.scene())>2,"Sliding along a vertical or dynamic receiver leaves a trail");
    }
    model.clear(); auto f=dragging(); f.bodies[0].normal={1,0,0};
    for (unsigned height=0;height<20;++height) for (unsigned repeat=0;repeat<4;++repeat) {
        f.bailed=false; model.step(f,{},true,false,1,1); f.bailed=true;
        f.bodies[0].contact_point[1]=float(height)*2;
        f.bodies[0].pose[13]=float(height)*2;
        auto h=hit(f); model.step(f,{&h,1},true,false,1,1);
    }
    check(marks(model.scene())==60 && model.scene().stats.density_rejected==20,
        "Wall density caps overlapping stains while keeping distinct heights independent");
}
void receiver_lifetimes() {
#ifdef _WIN32
    {
        namespace decal=dingosdk::decals::native;
        decal::Properties props; props.mask=1; props.flags=0x41; props.material=123; props.enabled=true;
        decal::assign_mask(props,decal::ReceiverMask::all);
        check(props.mask==0 && props.flags==0x2041 && props.material==123 && props.enabled,
            "Blood overrides the static-only receiver mask without discarding material or enable flags");
    }
#endif
    struct ReceiverFake:FakeGround {
        BloodReceiverId id{22,0x100000003};
        BloodMatrix pose{1,0,0,0,0,1,0,0,0,0,1,0,10,20,30,1},drawn{};
        unsigned moved{},bound{}; bool exists=true,can_bind=true;
        BloodGroundFunctions functions() {
            auto f=FakeGround::functions();
            f.bind=[](void* c,const BloodMark&,BloodReceiver& out) {
                auto& s=*static_cast<ReceiverFake*>(static_cast<FakeGround*>(c)); ++s.bound;
                out={s.id,s.pose}; return s.exists && s.can_bind;
            };
            f.pose=[](void* c,const BloodReceiverId& id,BloodMatrix& out) {
                auto& s=*static_cast<ReceiverFake*>(static_cast<FakeGround*>(c)); out=s.pose;
                return s.exists && id==s.id;
            };
            f.move=[](void* c,std::uint32_t handle,const BloodMatrix& transform) {
                auto& s=*static_cast<ReceiverFake*>(static_cast<FakeGround*>(c));
                check(s.live.contains(handle),"Transforms update only owned live decals"); ++s.moved; s.drawn=transform;
            };
            return f;
        }
    } fake;
    auto scene=std::make_unique<BloodGroundScene>(); scene->entity=11; scene->world=22; scene->generation=1;
    auto storage=std::make_unique<BloodGroundPool>(); auto& pool=*storage; const auto functions=fake.functions();
    auto& mark=scene->marks[0]; mark.id=1;
    mark.transform={.2f,0,0,0,0,.08f,0,0,0,0,.1f,0,12,20,30,1};
    pool.update(*scene,functions,true); pool.update(*scene,functions,true);
    check(fake.created==1 && fake.bound==1 && fake.moved==0 && pool.attached()==1,
        "A stationary prop binds once without redundant native transform writes");
    fake.pose[12]+=5; pool.update(*scene,functions,true);
    check(fake.moved==1 && std::abs(fake.drawn[12]-17)<.0001f,"Car translation carries the original stain without reallocating it");
    const float half=std::sqrt(.5f);
    check(blood_surface::pose({0,0,half,half},{15,20,30,0},fake.pose),"Native XYZW quaternion and position form a valid rigid pose");
    pool.update(*scene,functions,true);
    check(fake.moved==2 && std::abs(fake.drawn[12]-15)<.0001f && std::abs(fake.drawn[13]-22)<.0001f &&
        std::abs(fake.drawn[0])<.0001f && std::abs(fake.drawn[1]-.2f)<.0001f,
        "Receiver rotation carries both the stain centre and its projection axes");
    mark.age=blood_mark_lifetime-2.5f; pool.update(*scene,functions,true);
    check(fake.updated==1 && fake.created==1 && fake.moved==2,"Attached marks keep lifetime fading without extra allocations or moves");
    fake.exists=false; pool.update(*scene,functions,true); fake.exists=true;
    pool.update(*scene,functions,true);
    check(pool.active()==0 && fake.released==1 && fake.created==1,"Deleted receivers release their decals without replaying a stale mark");
    ++mark.id; mark.age=0; pool.update(*scene,functions,true);
    fake.id[1]+=0x100000000; pool.update(*scene,functions,true);
    check(pool.active()==0 && fake.released==2,"A recycled body index with a new generation cannot inherit the previous receiver's blood");
    ++mark.id; fake.can_bind=false; pool.update(*scene,functions,true); pool.update(*scene,functions,true);
    check(fake.created==2 && pool.failed()==1,"Unresolved receivers are skipped once instead of leaving floating decals");
    ++mark.id; fake.can_bind=true; fake.pose[0]=2; pool.update(*scene,functions,true);
    check(fake.created==2 && pool.failed()==1,"Invalid receiver transforms cannot create a decal");
    blood_surface::pose({0,0,0,1},{0,0,0,0},fake.pose); ++mark.id; pool.update(*scene,functions,true);
    fake.pose[12]=std::numeric_limits<float>::quiet_NaN(); pool.update(*scene,functions,true);
    check(pool.active()==0 && fake.released==3,"A receiver that loses its valid pose releases its attached mark");
    blood_surface::pose({0,0,0,1},{0,0,0,0},fake.pose); ++mark.id; pool.update(*scene,functions,true);
    ++scene->world; mark={}; pool.update(*scene,functions,true);
    check(fake.live.empty() && pool.attached()==0,"World changes clear receiver attachments and native ownership");
    BloodMatrix pose;
    check(!blood_surface::pose({0,0,0,0},{0,0,0,0},pose),"Invalid native rotations are rejected before projection");
}
void blood_damage_gate() {
    auto storage=std::make_unique<BloodModel>(); auto& model=*storage; auto f=dragging(); auto h=hit(f);
    BloodOptions options{true,false,1,100}; h.severity=99;
    model.step(f,{&h,1},options,1);
    check(count(model.scene())==0 && marks(model.scene().ground)==0,"A hit below the blood threshold creates neither particles nor a ground wound");
    h.severity=100; h.new_episode=false;
    model.step(f,{&h,1},options,1);
    check(model.scene().accepted==1 && count(model.scene())==3 && marks(model.scene().ground)==1,
        "An impact crossing the threshold later in its episode starts both blood paths");
    h.severity=400; model.step(f,{&h,1},options,1);
    check(model.scene().accepted==1 && marks(model.scene().ground)==1,"Further peak updates do not repeat the same blood impact");
    f.bailed=false; options.minimum_damage=500;
    model.step(f,{},options,1);
    check(marks(model.scene().ground)==1,"Raising the threshold does not erase existing ground blood");
    model.clear(); f=dragging(); h=hit(f); h.severity=3599; options.minimum_damage=3600;
    model.step(f,{&h,1},options,1);
    check(model.scene().accepted==0 && marks(model.scene().ground)==0,"The maximum threshold rejects weaker impacts");
    h.severity=3600; h.new_episode=false; model.step(f,{&h,1},options,1);
    check(model.scene().accepted==1 && marks(model.scene().ground)==1,"The maximum achievable impact qualifies at the upper slider limit");
    model.clear(); options.minimum_damage=0; h.severity=0; model.step(f,{&h,1},options,1);
    check(model.scene().accepted==0 && marks(model.scene().ground)==0,"Zero threshold still requires positive impact damage");
    h.severity=1; model.step(f,{&h,1},options,1);
    check(model.scene().accepted==1 && marks(model.scene().ground)==1,"Zero threshold accepts a light positive impact");
    options.minimum_damage=std::numeric_limits<float>::quiet_NaN(); model.step(f,{&h,1},options,1);
    check(count(model.scene())==0 && marks(model.scene().ground)==0,"Invalid blood thresholds cannot enable unrestricted emission");
}
void options() {
    Options v; v.blood=false; v.blood_strength=.15f; v.blood_min_damage=3600;
    check(decode_options(encode_options(v))==std::optional(v),"Blood preferences survive profile serialization");
    const auto legacy=decode_options(R"({"version":1,"blood":false})");
    check(legacy && legacy->blood_min_damage==25,"Existing profiles keep the previous blood threshold");
    v.blood_min_damage=-1; check(!valid_options(v),"Negative blood damage thresholds are invalid");
    v.blood_min_damage=3601; check(!valid_options(v),"Blood damage thresholds stay within the impact range");
    v.blood_min_damage=std::numeric_limits<float>::quiet_NaN(); check(!valid_options(v),"Nonfinite blood damage thresholds are invalid");
    v.blood_min_damage=25;
    v.blood_strength=std::numeric_limits<float>::quiet_NaN(); check(!valid_options(v),"Nonfinite blood amount is invalid");
    v.blood_strength=1.01f; check(!valid_options(v),"Blood amount is bounded");
}
}
namespace {
void customization() {
    const auto axis_length=[](const auto& t,unsigned i){return std::sqrt(t[i]*t[i]+t[i+1]*t[i+1]+t[i+2]*t[i+2]);};
    Options v; v.blood_tuning={2,.5f,3,4,150,BloodColor::pink};
    check(decode_options(encode_options(v))==v,"Blood customization survives a profile round-trip");
    const auto legacy=decode_options(R"({"version":1,"blood":false})");
    check(legacy && legacy->blood_tuning==BloodTuning{},"Older profiles retain original red blood and trail behavior");
    for (unsigned i=0;i<4;++i) {
        v.blood_tuning.color=static_cast<BloodColor>(i);
        check(decode_options(encode_options(v))==v,"Every blood color persists");
    }
    auto encoded=encode_options(v);
    auto pos=encoded.find("\"bloodColor\":3"); encoded.replace(pos,14,"\"bloodColor\":4");
    check(!decode_options(encoded),"Unknown blood colors are rejected");
    for (auto member:{&BloodTuning::density,&BloodTuning::width,&BloodTuning::length,&BloodTuning::bleeding,&BloodTuning::lifetime}) {
        v.blood_tuning={}; v.blood_tuning.*member=std::numeric_limits<float>::quiet_NaN();
        check(!valid_options(v),"Nonfinite blood tuning cannot enter simulation");
        v.blood_tuning.*member=0; check(!valid_options(v),"Zero blood tuning is rejected");
        v.blood_tuning.*member=301; check(!valid_options(v),"Out-of-range blood tuning is rejected");
    }
    check(blood_colored_asset(blood_ground_materials[0],BloodColor::green)=="world/materials/decal/reskate_blood/reskate_blood_green_dv",
        "Ground color suffix precedes the decal suffix");
    check(blood_colored_asset(blood_asset_names[8],BloodColor::pink)=="effects/reskate/hallofmeat/ebp_blood_trail_heavy_pink",
        "Particle colors retain their impact tier");
    check(blood_pixel(BloodColor::red,72,3,6)==std::array<std::uint8_t,3>{72,3,6} &&
        blood_pixel(BloodColor::green,72,3,6)==std::array<std::uint8_t,3>{3,72,6} &&
        blood_pixel(BloodColor::blue,72,3,6)==std::array<std::uint8_t,3>{3,6,72} &&
        blood_pixel(BloodColor::pink,72,3,6)==std::array<std::uint8_t,3>{72,18,54},"Preset pigments preserve shading and distinguish every color");
    auto storage=std::make_unique<BloodGroundModel>(); auto& model=*storage;
    auto f=dragging(); auto h=hit(f); BloodTuning tuning;
    model.step(f,{&h,1},true,false,1,1,25,tuning);
    const auto original=model.scene().marks[0];
    model.clear(); tuning.width=2; tuning.length=3; tuning.lifetime=10;
    model.step(f,{&h,1},true,false,1,1,25,tuning);
    const auto customized=model.scene().marks[0];
    check(std::abs(axis_length(customized.transform,0)/axis_length(original.transform,0)-3)<.001f &&
        std::abs(axis_length(customized.transform,8)/axis_length(original.transform,8)-2)<.001f &&
        axis_length(customized.transform,4)==axis_length(original.transform,4),"Smear sliders scale both in-plane axes while preserving projection depth");
    check(customized.lifetime==10,"New marks capture the selected lifetime");
    tuning.lifetime=300; f.bailed=false; f.dt=.1f;
    for (unsigned i=0;i<101;++i) model.step(f,{},true,false,1,1,25,tuning);
    check(marks(model.scene())==0,"Changing lifetime does not prolong old marks");
    auto faded=customized; faded.age=7.5f;
    check(std::abs(faded.opacity()-.5f)<.001f,"Custom lifetimes retain the five-second fade");
    std::array<std::size_t,2> densities{},bleeding{};
    for (unsigned mode=0;mode<2;++mode) {
        model.clear(); f=dragging(); h=hit(f); tuning={}; tuning.density=mode ? 3.f : .25f;
        model.step(f,{&h,1},true,false,1,1,25,tuning);
        for (unsigned i=0;i<100;++i) {advance(f,.03f); model.step(f,{},true,false,1,1,25,tuning);}
        densities[mode]=marks(model.scene());
        model.clear(); f=dragging(); h=hit(f); tuning={}; tuning.bleeding=mode ? 5.f : .25f;
        model.step(f,{&h,1},true,false,1,1,25,tuning);
        for (unsigned i=0;i<200;++i) model.step(f,{},true,false,1,1,25,tuning);
        const auto before=marks(model.scene());
        for (unsigned i=0;i<50;++i) {advance(f,.03f); model.step(f,{},true,false,1,1,25,tuning);}
        bleeding[mode]=marks(model.scene())-before;
    }
    check(densities[1]>densities[0],"Higher trail density leaves more marks over the same path");
    check(bleeding[0]==0 && bleeding[1]>0,"Bleeding multiplier changes the injury deposition window");
    auto combined=std::make_unique<BloodModel>(); f=dragging(); h=hit(f); BloodOptions options{true,false,1};
    combined->step(f,{&h,1},options,1);
    check(count(combined->scene())>0 && marks(combined->scene().ground)>0,"Color lifecycle starts with active blood");
    options.tuning.color=BloodColor::blue; combined->step(f,{},options,1);
    check(count(combined->scene())==0 && marks(combined->scene().ground)==0 && combined->scene().color==BloodColor::blue &&
        combined->scene().ground.color==BloodColor::blue,"Color changes clear both old particle and ground scenes");
    combined->step(f,{&h,1},options,1);
    check(count(combined->scene())>0 && marks(combined->scene().ground)>0,"New impacts resume with the selected color");
}
}

int main() {
    tracker_lifecycle();customization(); physical_contacts(); sources(); budgets_and_time(); native_lifetimes(); impact_strength(); ground_trails(); torso_trails(); ground_lifetimes(); ground_density_and_fades(); ground_burst_pressure(); ground_snapshot_replacement(); ground_impact_priority(); ground_footprints_and_grid(); draw_budget_ownership(); surface_orientations(); receiver_lifetimes(); blood_damage_gate(); options(); return failures ? 1 : 0;}
