#include "Extension/Slam/slam_visuals.h"
#include <cmath>
#include <iostream>
#include <limits>

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
    check(decode_visual_options(encode_visual_options(options))==std::optional(options),
        "Reduced effects, timing and opacity survive together");
    check(!decode_visual_options("{}") && !decode_visual_options("{\"version\":2}"),"Missing and unsupported settings cannot replace defaults");
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
int main() {
    saved_options(); visibility_and_flashes(); normal_play_falls();
    if (failures) return 1;
    std::cout<<"Slam visual timing, accessibility and saved-options checks passed.\n";
}
