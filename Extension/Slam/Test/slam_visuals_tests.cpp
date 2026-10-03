#include "Extension/Slam/slam_visuals.h"
#include "Engine/Game/Settings/transient_float_lease.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <algorithm>

namespace {
using namespace dingosdk::slam;
int failures{};
void check(bool value,const char* message) {
    if (!value) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
void saved_options() {
    const VisualOptions defaults;
    check(valid_visual_options(defaults) && decode_visual_options(encode_visual_options(defaults))==std::optional(defaults),
        "Default X-ray options survive a save and reload");
    auto options=defaults; options.visibility=XrayVisibility::impact; options.reduced_effects=true;
    options.opacity=.1f; options.flash_strength=1; options.impact_duration_s=5; options.only_impacted=true; options.normal_play=true;
    options.impact_sound=false; options.fracture_marks=false; options.sound_volume=.2f;
    options.slow_motion=false; options.slow_motion_scale=.5f; options.slow_motion_seconds=.8f;
    options.impact_camera=false; options.impact_camera_strength=.4f;
    check(decode_visual_options(encode_visual_options(options))==std::optional(options),
        "Reduced effects, timing and opacity survive together");
    check(!decode_visual_options("{}") && !decode_visual_options("{\"version\":2}"),"Missing and unsupported settings cannot replace defaults");
    check(decode_visual_options(R"({"version":1,"visibility":0,"opacity":0.86,"flash":0.35,"impactSeconds":1.5,"reduced":false})")==std::optional(defaults),
        "Original X-ray saves gain the new impact options without losing their saved settings");
    const auto encoded=encode_visual_options(options);
    auto legacy=encoded; const auto added=legacy.find("\"onlyImpacted\":true,"); legacy.erase(added,20);
    auto old=options; old.only_impacted=false;
    check(decode_visual_options(legacy)==std::optional(old),"Existing X-ray saves restore without the new optional field");
    const auto normal=legacy.find("\"normalPlay\":true,"); legacy.erase(normal,18);
    old.normal_play=false;
    check(decode_visual_options(legacy)==std::optional(old),"Old saves keep standalone X-ray off until the player enables it");
    auto bad_normal=encoded; const auto normal_flag=bad_normal.find("\"normalPlay\":true"); bad_normal.replace(normal_flag,17,"\"normalPlay\":1");
    check(!decode_visual_options(bad_normal),"Standalone X-ray accepts a saved boolean only");
    auto bad=encoded; const auto reduced=bad.find("\"reduced\":true")+10; bad.replace(reduced,4,"1");
    check(!decode_visual_options(bad),"Reduced effects accepts a saved boolean only");
    bad=encoded; const auto visibility=bad.find("\"visibility\":2"); bad.replace(visibility,14,"\"visibility\":9");
    check(!decode_visual_options(bad),"An invalid visibility mode cannot be restored");
    options.opacity=std::numeric_limits<float>::quiet_NaN();
    check(!valid_visual_options(options),"A non-finite opacity is invalid");
    bool rejected{};
    try {(void)encode_visual_options(options);} catch (...) {rejected=true;}
    check(rejected,"Invalid visual options cannot be saved");
    check(!decode_visual_options(std::string(513,' ')),"Oversized saved documents fail safely");
}
void normal_play_falls() {
    FreeplayXray xray;
    Frame frame; frame.valid=true; frame.entity=11; frame.world=22; frame.dt=.02f;
    frame.center={0,10,0}; frame.body_count=1; frame.bodies[0].region=Region::head; frame.bodies[0].joint=102;
    VisualOptions options;
    check(visual_appearance(options,xray.result(),xray.events(),1000,false,true).opacity==options.opacity,
        "Always-on normal X-ray does not require a started challenge");
    check(visual_appearance(options,xray.result(),xray.events(),1000,false).opacity==0,
        "Slam still requires an attempt when standalone mode is off");
    options.visibility=XrayVisibility::impact; options.only_impacted=true;
    for (unsigned fall=0;fall<3;++fall) {
        const auto at=1000+fall*2000;
        frame.bailed=false; frame.grounded=false; frame.bodies[0].contact=false; frame.bodies[0].velocity={0,-20,0};
        xray.step(frame,at);
        check(xray.result().phase==Phase::attempt && !xray.result().impacts,"Normal play automatically arms the next fall with clean injuries");
        check(visual_appearance(options,xray.result(),xray.events(),at,false,true).opacity==0,"Each new fall waits for a fresh impact");
        frame.bailed=true; frame.grounded=true; frame.bodies[0].contact=true; frame.bodies[0].normal={0,1,0}; frame.bodies[0].velocity={};
        xray.step(frame,at+20);
        const auto hit=visual_appearance(options,xray.result(),xray.events(),at+20,false,true);
        check(xray.result().impacts==1 && hit.colors[102][3]>0 && hit.colors[101][3]==0,"Repeated normal falls reveal only the newly impacted bone");
        check(visual_appearance(options,xray.result(),xray.events(),at+1520,false,true).opacity==0,"Normal impacts fade without any menu action");
        frame.bailed=false; xray.step(frame,at+1540);
    }
    frame.bailed=false; frame.bodies[0].contact=false; xray.step(frame,7000);
    frame.world=23; xray.step(frame,7020);
    check(xray.result().phase==Phase::ready && !xray.events().latest_impact_ms && !xray.result().impacts,
        "Changing the world drops standalone injuries instead of reusing the completed fall");
    xray.reset();
    check(xray.result().phase==Phase::ready && !xray.events().latest_impact_ms,"Disabling or losing the skater clears standalone injuries");
    frame.bailed=true; xray.step(frame,8000);
    check(xray.result().phase==Phase::ready,"Enabling while already down cannot create an unmeasured impact");
    options.visibility=XrayVisibility::attempt;
    check(visual_appearance(options,xray.result(),xray.events(),8000,false,true).opacity>0,"Always-on X-ray still shows the skeleton when enabled while down");
    check(visual_appearance(options,xray.result(),xray.events(),8000,true,true).opacity==0,"First-person camera hides standalone X-ray too");
    options.visibility=XrayVisibility::off;
    check(visual_appearance(options,xray.result(),xray.events(),8000,false,true).opacity==0,"Off overrides standalone X-ray");
}
void impact_feedback() {
    VisualOptions options;
    Result result; result.phase=Phase::bailed;
    VisualEvents events;
    result.impacts=1; result.bone_injuries[102]={80,0,false}; events.observe(result,1000);
    check(events.latest_bone==102 && events.latest_severity==80 && !events.latest_fracture,
        "A contact sound identifies the affected bone and new severity");
    check(visual_appearance(options,result,events,1000,false).damage[102][1]>0 &&
        visual_appearance(options,result,events,1000,false).damage[277][1]==0,
        "The sharp impact pulse affects only contacted bones");
    result.impacts=3; result.bone_injuries[102].severity=400; result.bone_injuries[277]={250,0,true};
    events.observe(result,1200);
    check(events.latest_bone==277 && events.latest_fracture && events.latest_severity==250,
        "A new fracture takes sound priority over a stronger repeat hit in the same batch");
    check(events.fractures_at_ms[277]==1200 && visual_appearance(options,result,events,1200,false).damage[277][3]==1,
        "A fractured bone opens once using its own fracture time");
    ++result.impacts; result.bone_injuries[277].severity+=10; events.observe(result,1250);
    check(events.fractures_at_ms[277]==1200 && !events.latest_fracture,
        "Repeated contact with a broken bone cannot restart the fracture opening");
    const auto recorded=events;
    events.observe(result,1260);
    check(events.latest_impact_ms==recorded.latest_impact_ms && events.latest_severity==10,
        "Republishing an unchanged impact does not retrigger sound or flash timing");
    const auto late=visual_appearance(options,result,events,1600,false);
    check(late.damage[277][0]==1 && late.damage[277][1]==0 && late.damage[277][3]==0,"Fracture marks remain after the short hit pulse and fracture opening expire");
    options.reduced_effects=true;
    const auto reduced=visual_appearance(options,result,events,1200,false);
    check(reduced.damage[277][0]==1 && reduced.damage[277][1]==0 && reduced.damage[277][3]==0,"Reduced effects keeps fracture marks without a bright flash or animated opening");
    options.fracture_marks=false;
    check(visual_appearance(options,result,events,1200,false).damage[277][0]==0,"Fracture marks have an independent off switch");
    for (const bool fracture : {false,true}) {
        const auto pcm=make_impact_sound(fracture);
        const auto peak=std::max_element(pcm.begin(),pcm.end(),[](auto a,auto b) {return std::abs(static_cast<int>(a))<std::abs(static_cast<int>(b));});
        check(pcm.size()==9600 && pcm.front()==0 && pcm.back()==0 && std::abs(static_cast<int>(*peak))<=26214,
            "Synthesized impact clips are short, bounded below full scale and start/end without a sample jump");
        double early{},late_energy{};
        for (std::size_t i=0;i<pcm.size();++i) {
            const double energy=static_cast<double>(pcm[i])*pcm[i];
            if (i<2400) early+=energy;
            if (i>=7200) late_energy+=energy;
        }
        check(early>late_energy*20,"The impact's attack decays instead of sustaining an alarm-like sound");
    }
    options.sound_volume=std::numeric_limits<float>::infinity();
    check(!valid_visual_options(options),"Non-finite sound gain cannot reach playback");
}
void time_scale_ownership() {
    using dingosdk::FloatSettingSample;
    using dingosdk::TransientFloatLease;
    FloatSettingSample engine{11,22,33,.6f};
    bool readable=true,accepted=true,owns_during_write{};
    unsigned writes{};
    TransientFloatLease lease;
    const auto read=[&]()->std::optional<FloatSettingSample> {return readable ? std::optional(engine) : std::nullopt;};
    const auto write=[&](const FloatSettingSample& expected,float value) {
        ++writes;
        owns_during_write=lease.owns(engine);
        if (!accepted || engine!=expected) return false;
        engine.value=value; return true;
    };
    check(lease.begin(.3f,read,write) && std::abs(engine.value-.18f)<.0001f && owns_during_write,
        "The pulse multiplies the player's current game speed and owns its pre-write sample");
    check(lease.update(.5f,read,write) && std::abs(engine.value-.3f)<.0001f && owns_during_write,
        "Recovery uses the original speed, without multiplying the already slowed value");
    check(lease.restore(read,write) && !lease.active() && engine.value==.6f,"Pulse completion restores the original non-default game speed");
    check(lease.begin(.3f,read,write),"A second impact can acquire the restored setting");
    engine.value=.8f; const auto before_external=writes;
    check(lease.restore(read,write) && !lease.active() && engine.value==.8f && writes==before_external,
        "A newer external speed is preserved without writing the old original over it");
    check(lease.begin(.3f,read,write),"A pulse can begin over the new player speed");
    engine.address=44; const auto before_reuse=writes;
    check(!lease.update(.4f,read,write) && !lease.active() && writes==before_reuse,
        "A replaced native field cannot receive an update from its old lease");
    engine.value=.6f;
    check(lease.begin(.3f,read,write),"A fresh native field gets an independent lease");
    readable=false;
    check(!lease.update(.4f,read,write) && !lease.restore(read,write) && lease.active(),
        "A temporary unreadable field retains cleanup ownership for a later retry");
    readable=true;
    check(lease.restore(read,write) && engine.value==.6f,"A recovered native field can complete delayed cleanup");
    accepted=false;
    check(!lease.begin(.3f,read,write) && !lease.active() && engine.value==.6f,"A rejected setter cannot start a pulse or change the original speed");
    const auto before_invalid=writes;
    check(!lease.begin(0,read,write) && !lease.begin(std::numeric_limits<float>::quiet_NaN(),read,write) && writes==before_invalid,
        "Freeze and non-finite pulse factors cannot reach the native setter");
}
void slow_motion_envelope() {
    VisualOptions options; options.slow_motion_seconds=.5f;
    VisualEvents event; event.latest_impact_ms=1000; event.latest_severity=400;
    SlowMotionPulse pulse;
    const auto start=pulse.step(options,event,1000,true);
    check(start.active && start.started && start.factor==options.slow_motion_scale,"A fresh severe hit starts a bounded slow-motion pulse");
    const auto middle=pulse.step(options,event,1250,true);
    check(middle.active && !middle.started && middle.factor>start.factor && middle.factor<1,"The pulse returns smoothly toward the original speed");
    event.latest_impact_ms=1300;
    check(!pulse.step(options,event,1300,true).started && !pulse.step(options,event,1500,true).active,
        "Further ragdoll contacts cannot prolong the current pulse");
    event.latest_impact_ms=1600; event.latest_fracture=true; event.latest_severity=20;
    check(pulse.step(options,event,1600,true).started,"A new fracture can start the next pulse even at lower contact severity");
    check(!pulse.step(options,event,1650,false).active,"Focus loss, map change or a mode conflict cancels the pulse immediately");
    check(!pulse.step(options,event,1660,true).started,"Returning to play cannot replay the cancelled hit");
    event.latest_impact_ms=2200; options.reduced_effects=true;
    check(!pulse.step(options,event,2200,true).active,"Reduced effects prevents automatic slow motion");
    options.reduced_effects=false;
    check(!pulse.step(options,event,2250,true).started,"Turning effects back on cannot replay an old contact");
    event.latest_impact_ms=2500; event.latest_fracture=false; event.latest_severity=100;
    check(!pulse.step(options,event,2500,true).active,"Minor contacts do not slow normal skating");
    event.latest_impact_ms=2600; event.latest_severity=400;
    check(!pulse.step(options,event,2800,true).active,"A stale contact cannot trigger delayed slow motion");
    event.latest_impact_ms=3000;
    check(pulse.step(options,event,3000,true).active,"A fresh severe impact can begin after the cooldown");
    event.reset();
    check(!pulse.step(options,event,3010,true).active,"Recovery or dismissal clears the impact and immediately cancels slow motion");
}
void visibility_and_flashes() {
    VisualOptions options;
    VisualEvents events;
    Result result; result.phase=Phase::attempt;
    events.observe(result,1000);
    check(visual_appearance(options,result,events,1000,false).opacity==options.opacity,"Attempt mode shows the skeleton while skating");
    options.visibility=XrayVisibility::bail;
    check(visual_appearance(options,result,events,1000,false).opacity==0,"Bail mode hides it before a fall");
    result.phase=Phase::bailed;
    check(visual_appearance(options,result,events,1000,false).opacity==options.opacity,"Bail mode shows it after a bail without requiring an impact");
    options.visibility=XrayVisibility::impact;
    check(visual_appearance(options,result,events,1000,false).opacity==0,"Impact mode waits for a scored contact");
    result.impacts=1; result.injuries[0].severity=100; result.bone_injuries[102].severity=100;
    events.observe(result,2000);
    auto fresh=visual_appearance(options,result,events,2000,false);
    check(fresh.opacity==options.opacity && fresh.colors[102][1]>.55f,"The affected bone receives a local impact tint");
    check(fresh.colors[7][0]==.72f,"An impact cannot flash unrelated bones");
    options.only_impacted=true;
    const auto isolated=visual_appearance(options,result,events,2000,false);
    check(isolated.colors[102][3]==options.opacity && isolated.colors[101][3]==0 && isolated.colors[7][3]==0,
        "Only the impacted bone is visible, including within the same body region");
    options.only_impacted=false;
    const auto unchanged=events;
    events.observe(result,2200);
    check(events.impacts_at_ms==unchanged.impacts_at_ms,"Publishing the same impact does not restart a flash");
    result.phase=Phase::results;
    const auto expired_flash=visual_appearance(options,result,events,2700,false);
    check(std::abs(expired_flash.colors[102][1]-.55f)<.001f,"An impact flash fades even when the score stops updating at Results");
    const auto fading=visual_appearance(options,result,events,3300,false);
    check(fading.opacity>0 && fading.opacity<options.opacity,"Timed visibility fades smoothly before expiring");
    check(visual_appearance(options,result,events,3500,false).opacity==0,"Timed visibility expires at its configured duration");
    result.impacts=2; result.injuries[2].severity=300; result.injuries[2].fractured=true;
    result.bone_injuries[277]={300,0,true};
    events.observe(result,4000);
    options.reduced_effects=true;
    const auto reduced=visual_appearance(options,result,events,4000,false);
    check(reduced.colors[277][0]==1 && reduced.colors[277][1]==.16f && reduced.colors[277][2]==.22f,
        "Reduced effects retains the steady fracture color and removes transient tints");
    check(events.impacts_at_ms[102]==2000 && events.impacts_at_ms[277]==4000,"A second bone keeps its own impact time");
    check(visual_appearance(options,result,events,4000,true).opacity==0,"First person hides the mesh before it can cover the camera");
    options.visibility=XrayVisibility::off;
    check(visual_appearance(options,result,events,4000,false).opacity==0,"Off disables only the X-ray presentation");
    options.visibility=XrayVisibility::attempt;
    result.cancelled=true; events.observe(result,4500);
    check(!events.latest_impact_ms && visual_appearance(options,result,events,4500,false).opacity==0,
        "Stopping, unloading or losing the skater clears effects");
    result.cancelled=false; result.phase=Phase::attempt; result.impacts=0; result.injuries={}; result.bone_injuries={};
    events.observe(result,5000);
    options.visibility=XrayVisibility::impact;
    check(visual_appearance(options,result,events,5000,false).opacity==0,"A fresh attempt cannot inherit the last attempt's flash");
    result.impacts=1; result.injuries[0].severity=50; result.bone_injuries[102].severity=50; events.observe(result,6000);
    result.impacts=0; result.injuries={}; result.bone_injuries={}; events.observe(result,6100);
    check(!events.latest_impact_ms,"Retry clears impact times before a new fall");
}
}
void impact_camera_timing() {
    VisualOptions options; VisualEvents event;
    event.latest_impact_ms=1000; event.latest_severity=500;
    ImpactCameraPulse pulse;
    const auto impulse=pulse.step(options,event,1000,true);
    const auto middle=sample_camera_impulse(impulse,1100);
    check(impulse.started_ms==1000 && middle.active && middle.fov_scale<1 && middle.fov_scale>=.8f &&
        std::abs(middle.translation[0])<.05f && std::abs(middle.roll)<.03f,"Severe hits produce a bounded local camera response");
    event.latest_impact_ms=1200;
    check(pulse.step(options,event,1200,true).started_ms==1000 && !sample_camera_impulse(impulse,1450).active,
        "Further contacts do not extend the camera response beyond its original end");
    event.latest_impact_ms=1600; event.latest_fracture=true;
    check(pulse.step(options,event,1600,true).started_ms==1600,"A later fracture can start a new camera response");
    check(!pulse.step(options,event,1610,false).started_ms && !pulse.step(options,event,1620,true).started_ms,
        "Focus or mode cancellation cannot replay a previous camera impact when play resumes");
    options.reduced_effects=true; event.latest_impact_ms=2200;
    check(!pulse.step(options,event,2200,true).started_ms,"Reduced effects disables automatic camera response");
    options.reduced_effects=false; event.latest_impact_ms=2300; event.latest_fracture=false; event.latest_severity=100;
    check(!pulse.step(options,event,2300,true).started_ms,"Minor contacts cannot shake the normal skating camera");
}
int main() {
    saved_options(); visibility_and_flashes(); normal_play_falls(); impact_feedback(); time_scale_ownership(); slow_motion_envelope(); impact_camera_timing();
    if (failures) return 1;
    std::cout<<"Slam visual timing, accessibility and saved-options checks passed.\n";
}
