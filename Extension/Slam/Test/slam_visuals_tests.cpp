#include "Extension/Slam/slam_visuals.h"
#include "Engine/Game/Settings/transient_float_lease.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

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
    options.fracture_sound=false; options.fracture_volume=.75f; options.fracture_sound_path="sounds/bone crack.wav";
    options.slow_motion_hold=.7f; options.effect_severity=900; options.player_zoom=false; options.player_zoom_strength=.4f;
    options.pass_out=false; options.pass_out_hud=false; options.pass_out_strength=1; options.pass_out_seconds=4; options.pass_out_severity=800;
    options.player_zoom_ease_seconds=.5f; options.player_follow_seconds=.3f; options.pass_out_redness=.65f;
    options.only_fractured=true;
    check(decode_visual_options(encode_visual_options(options))==std::optional(options),
        "Reduced effects, timing and opacity survive together");
    check(!decode_visual_options("{}") && !decode_visual_options("{\"version\":2}"),"Missing and unsupported settings cannot replace defaults");
    check(decode_visual_options(R"({"version":1,"visibility":0,"opacity":0.86,"flash":0.35,"impactSeconds":1.5,"reduced":false})")==std::optional(defaults),
        "Original X-ray saves gain the new impact options without losing their saved settings");
    const auto encoded=encode_visual_options(options);
    for (unsigned mode=0;mode<4;++mode) {
        auto saved=encoded;
        const auto at=saved.find("\"visibility\":2");
        saved.replace(at,14,"\"visibility\":"+std::to_string(mode));
        const auto restored=decode_visual_options(saved);
        auto expected=options;
        expected.visibility=mode==1 ? XrayVisibility::impact : static_cast<XrayVisibility>(mode);
        check(restored==std::optional(expected),"Saved Always and Off choices are preserved; only After bailing migrates to After impacts");
        if (restored) check(decode_visual_options(encode_visual_options(*restored))==restored,
            "Each supported visibility choice survives saving again without losing fade duration or filters");
    }
    auto before_fracture_filter=encoded;
    const auto filter_at=before_fracture_filter.find("\"onlyFractured\":true,");
    before_fracture_filter.erase(filter_at,std::string_view("\"onlyFractured\":true,").size());
    auto without_filter=options; without_filter.only_fractured=false;
    check(decode_visual_options(before_fracture_filter)==std::optional(without_filter),
        "Existing visual saves keep the fracture-only filter off and preserve other preferences");
    auto invalid_filter=encoded;
    invalid_filter.replace(invalid_filter.find("\"onlyFractured\":true"),std::string_view("\"onlyFractured\":true").size(),"\"onlyFractured\":1");
    check(!decode_visual_options(invalid_filter),"The fracture-only option accepts a saved boolean only");
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
    check(!decode_visual_options(std::string(4097,' ')),"Oversized saved documents fail safely");
    const auto muted=decode_visual_options(R"({"version":1,"visibility":0,"opacity":0.86,"flash":0.35,"impactSeconds":1.5,"reduced":false,"impactSound":false,"soundVolume":0.2,"slowMotionSeconds":0.45})");
    check(muted && !muted->impact_sound && !muted->fracture_sound && muted->fracture_volume==.2f && muted->slow_motion_seconds==.45f,
        "Upgrading preserves previous sound-off, volume and slow-motion duration preferences");
    for (const auto name : {"fractureVolume","slowMotionHold","effectSeverity","playerZoomStrength","passOutStrength","passOutSeconds","passOutSeverity",
        "playerZoomEaseSeconds","playerFollowSeconds","passOutRedness"}) {
        auto bad_number=encoded;
        const auto at=bad_number.find(std::string("\"")+name+"\":")+std::strlen(name)+3;
        const auto end=bad_number.find_first_of(",}",at);
        bad_number.replace(at,end-at,"99999");
        check(!decode_visual_options(bad_number),"Out-of-range presentation controls cannot be restored");
    }
    options=defaults; options.fracture_sound_path=std::string(241,'x');
    check(!valid_visual_options(options),"Custom sound paths are bounded to the menu's buffer size");
    auto previous=encoded;
    for (const auto name : {"playerZoomEaseSeconds","playerFollowSeconds","passOutRedness"}) {
        const auto at=previous.find(std::string("\"")+name+"\":");
        const auto end=previous.find_first_of(",}",at);
        previous.erase(at,end-at+1);
    }
    auto upgraded=decode_visual_options(previous);
    check(upgraded && upgraded->player_zoom_ease_seconds==defaults.player_zoom_ease_seconds &&
        upgraded->player_follow_seconds==defaults.player_follow_seconds && upgraded->pass_out_redness==defaults.pass_out_redness,
        "Earlier visual saves receive smooth tracking and a subtle red tint without losing settings");
    options=defaults; options.pass_out_redness=std::numeric_limits<float>::quiet_NaN();
    check(!valid_visual_options(options),"A non-finite fade tint cannot reach rendering");
}
void normal_play_falls() {
    FreeplayXray xray;
    Frame frame; frame.valid=true; frame.entity=11; frame.world=22; frame.dt=.02f;
    frame.center={0,10,0}; frame.body_count=1; frame.bodies[0].region=Region::head; frame.bodies[0].joint=102;
    VisualOptions options;
    check(visual_appearance(options,xray.result(),xray.events(),1000,false,true).opacity==options.opacity,
        "Always shows the normal-play skeleton without waiting for an impact");
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
    check(visual_appearance(options,xray.result(),xray.events(),8000,false,true).opacity==0,"Enabling while down waits for a measured impact before showing bones");
    check(visual_appearance(options,xray.result(),xray.events(),8000,true,true).opacity==0,"First-person camera hides standalone X-ray too");
}
void normal_play_recovery_fade() {
    for (const float duration : {.3f,1.5f,5.f}) {
        FreeplayXray recovered,bailed;
        Frame frame; frame.valid=true; frame.entity=11; frame.world=22; frame.dt=.02f;
        frame.center={0,10,0}; frame.body_count=1;
        frame.bodies[0].region=Region::head; frame.bodies[0].joint=102;
        frame.bodies[0].velocity={0,-20,0};
        recovered.step(frame,1000); bailed.step(frame,1000);
        frame.bailed=true; frame.grounded=true; frame.bodies[0].contact=true;
        frame.bodies[0].normal={0,1,0}; frame.bodies[0].velocity={};
        recovered.step(frame,1020); bailed.step(frame,1020);
        VisualOptions options;
        options.visibility=XrayVisibility::impact; options.only_impacted=true; options.impact_duration_s=duration;
        const auto appearance=[&](const FreeplayXray& xray,std::uint64_t now) {
            return visual_appearance(options,xray.impact_result(),xray.impact_events(),now,false,true);
        };
        const auto hit=appearance(recovered,1020);
        check(hit.colors[102][3]>0,"Recovery fade starts with a visible injured bone");
        frame.bailed=false; recovered.step(frame,1040); recovered.step(frame,1060);
        check(recovered.result().phase==Phase::attempt && !recovered.result().impacts && !recovered.events().latest_impact_ms,
            "Recovery rearms scoring and clears live feedback while the previous impact can still fade");
        check(appearance(recovered,1060).colors==appearance(bailed,1060).colors,
            "Early recovery and rearming preserve the original bone colors and opacity");
        const auto middle=1020+static_cast<std::uint64_t>(duration*800);
        const auto fading=appearance(recovered,middle);
        check(fading.opacity>0 && fading.opacity<hit.opacity && fading.colors==appearance(bailed,middle).colors,
            "Recovered bones follow the same configured fade as bones while still bailed");
        const auto end=1020+static_cast<std::uint64_t>(std::ceil(duration*1000));
        check(appearance(recovered,end).opacity==0 && appearance(bailed,end).opacity==0,
            "Recovery neither truncates nor restarts the configured impact duration");

        // A second fall can begin before the retained presentation expires.
        frame.bodies[0].joint=277; frame.bodies[0].region=Region::left_arm;
        frame.bodies[0].contact=false; frame.bodies[0].velocity={0,-20,0};
        recovered.step(frame,1080);
        frame.bailed=true; frame.bodies[0].contact=true; frame.bodies[0].velocity={};
        recovered.step(frame,1100);
        const auto next=appearance(recovered,1100);
        check(recovered.result().impacts==1 && recovered.impact_events().latest_impact_ms==1100 &&
            next.colors[277][3]>0 && next.colors[102][3]==0,
            "An overlapping new fall replaces the retained injuries and starts its own fade");
        frame.entity=12; recovered.step(frame,1120);
        check(!recovered.impact_events().latest_impact_ms && appearance(recovered,1120).opacity==0,
            "Changing skaters immediately discards the retained impact presentation");
        bailed.reset();
        check(!bailed.impact_events().latest_impact_ms && appearance(bailed,1120).opacity==0,
            "Disabling X-ray clears the retained impact presentation");
    }
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
    result.bone_injuries[278]={260,0,true}; events.observe(result,1270);
    check(events.latest_bone==278 && events.latest_fracture && events.latest_impact_ms==1270 &&
        events.impacts_at_ms[278]==1270 && events.fractures_at_ms[278]==1270 &&
        visual_appearance(options,result,events,1270,false).damage[278][1]>0,
        "An injury without another scored impact gets its own flash, fracture time and sound event");
    events.observe(result,1290);
    check(events.latest_impact_ms==1270 && events.fractures_at_ms[278]==1270,
        "Publishing an unchanged injury does not restart contact feedback");
    const auto recorded=events;
    events.observe(result,1300);
    check(events.latest_impact_ms==recorded.latest_impact_ms && events.latest_severity==recorded.latest_severity,
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
    check(make_impact_sound(true,0)!=make_impact_sound(true,1),"New fractures have distinct crack textures");
    for (unsigned variant=0;variant<4;++variant) {
        const auto pcm=make_impact_sound(true,variant);
        check(pcm.front()==0 && pcm.back()==0 && std::all_of(pcm.begin(),pcm.end(),[](auto sample) {
            return std::abs(static_cast<int>(sample))<=26214;
        }),"Every fracture variant remains bounded and click-free at its endpoints");
    }
    options.sound_volume=std::numeric_limits<float>::infinity();
    check(!valid_visual_options(options),"Non-finite sound gain cannot reach playback");
}
void configurable_bone_damage() {
    VisualOptions options; options.reduced_effects=true; options.only_impacted=true;
    Result result; result.phase=Phase::bailed; result.config.scoring.bruise_threshold=100;
    VisualEvents events;
    result.bone_injuries[277].severity=99;
    events.observe(result,1000);
    const auto light=visual_appearance(options,result,events,1000,false);
    check(light.colors[277][3]==0 && light.damage[277][1]==0 && !is_bruised(result.bone_injuries[277],result.config.scoring),
        "Damage below the bruise threshold is not marked or revealed by contact-only X-ray");
    result.bone_injuries[277].severity=100;
    events.observe(result,1000);
    const auto bruised=visual_appearance(options,result,events,1000,false);
    check(bruised.colors[277][3]==options.opacity && bruised.colors[277][1]==.55f &&
        std::string_view(injury_state_name(result.bone_injuries[277],result.config.scoring))=="bruised",
        "Reaching the bruise threshold reveals the yellow injury and its result label");
    result.bone_injuries[277].fractured=true; options.only_fractured=true;
    events.observe(result,1000);
    const auto broken=visual_appearance(options,result,events,1000,false);
    check(broken.colors[277][3]==options.opacity && broken.colors[277][1]==.16f,
        "Fractures take precedence over bruises in fracture-only X-ray");

    FreeplayXray xray;
    Frame frame; frame.valid=true; frame.entity=11; frame.world=22; frame.dt=.02f;
    frame.center={0,10,0}; frame.body_count=1;
    auto& head=frame.bodies[0]; head.region=Region::head; head.joint=103; head.velocity={0,-20,0};
    ScoreRules rules; rules.bruise_threshold=500; rules.fracture_threshold=1000;
    xray.step(frame,1000);
    xray.step(frame,1020,rules);
    check(xray.result().config.scoring==rules,"Changing thresholds before a normal-play fall updates its armed rules");
    frame.bailed=true; frame.grounded=true; head.contact=true; head.normal={0,1,0}; head.velocity={};
    xray.step(frame,1040,rules);
    check(xray.result().bone_injuries[103].severity>0 && !is_bruised(xray.result().bone_injuries[103],rules) &&
        !xray.result().bone_injuries[103].fractured,"Normal-play impacts use the configured bruise and fracture thresholds");
    ScoreRules next_rules; next_rules.fracture_threshold=10;
    xray.step(frame,1060,next_rules);
    check(xray.result().config.scoring==rules && !xray.result().bone_injuries[103].fractured,
        "Changing thresholds during a fall cannot relabel it or retroactively trigger a crack");
    for (unsigned hit=0;hit<2;++hit) {
        head.contact=false; head.velocity={0,-20,0}; xray.step(frame,1080+hit*40,next_rules);
        head.contact=true; head.velocity={}; xray.step(frame,1100+hit*40,next_rules);
        const auto& injury=xray.result().bone_injuries[103];
        check(hit==0 ? is_bruised(injury,rules) && !injury.fractured : injury.fractured && xray.events().latest_fracture,
            "Separate confirmed hits accumulate first into a bruise, then into a fracture and its feedback event");
    }
    frame.bailed=false; xray.step(frame,1200,next_rules);
    xray.step(frame,1220,next_rules);
    check(xray.result().config.scoring==next_rules && xray.result().bone_injuries[103].severity==0,
        "Recovery arms the next fall with the latest damage thresholds and clean injuries");
}
void delayed_bail_feedback() {
    for (unsigned scenario=0;scenario<3;++scenario) {
        FreeplayXray xray;
        Frame frame; frame.valid=true; frame.entity=11; frame.world=22; frame.dt=.02f;
        frame.center={0,10,0}; frame.body_count=1;
        auto& head=frame.bodies[0]; head.region=Region::head; head.joint=102; head.injury_joint=103;
        head.velocity={0,-18,0};
        xray.step(frame,1000);
        head.contact=true; head.normal={0,1,0}; head.velocity={};
        xray.step(frame,1020);
        check(!xray.result().impacts && !xray.events().latest_impact_ms,
            "Upright impacts do not reveal injuries or trigger feedback before a bail");
        if (scenario==2) for (unsigned i=0;i<12;++i) xray.step(frame,1040+i*20);
        frame.bailed=true; frame.manual_bail=scenario==1;
        xray.step(frame,1300);
        check((xray.result().bone_injuries[103].severity>0)==(scenario==0),
            "Standalone X-ray attributes only recent contacts preceding a natural bail");
        check((xray.events().latest_bone==103 && xray.events().latest_impact_ms==1300)==(scenario==0),
            "Delayed skull damage starts feedback when the bail confirms the injury");
    }
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
    options.slow_motion_seconds=2; options.slow_motion_hold=.6f; options.effect_severity=700;
    event.latest_impact_ms=4000; event.latest_severity=400;
    check(!pulse.step(options,event,4000,true).active,"The saved severity threshold filters smaller hits");
    event.latest_impact_ms=4600; event.latest_severity=800;
    check(pulse.step(options,event,4600,true).started && pulse.step(options,event,5700,true).factor==options.slow_motion_scale,
        "A longer configured pulse holds slow speed for its chosen fraction");
    check(pulse.step(options,event,6200,true).factor>options.slow_motion_scale && !pulse.step(options,event,6600,true).active,
        "The configurable hold still ends with a bounded smooth recovery");
}
void custom_audio() {
    std::vector<std::byte> wav(52);
    const auto put=[&](std::size_t at,unsigned value,unsigned count) {
        for (unsigned i=0;i<count;++i) wav[at+i]=static_cast<std::byte>((value>>(i*8))&255);
    };
    std::memcpy(wav.data(),"RIFF",4); put(4,44,4); std::memcpy(wav.data()+8,"WAVEfmt ",8);
    put(16,16,4); put(20,1,2); put(22,1,2); put(24,48000,4); put(28,96000,4); put(32,2,2); put(34,16,2);
    std::memcpy(wav.data()+36,"data",4); put(40,8,4); put(46,1200,2);
    const auto samples=decode_impact_wav(wav);
    check(samples && samples->size()==4 && (*samples)[1]==1200,"A PCM16 mono 48 kHz replacement preserves its samples");
    const auto saved=wav;
    for (const auto [at,value] : {std::pair<std::size_t,unsigned>{20,3},{22,2},{24,44100},{34,8},{40,999999}}) {
        wav=saved; put(at,value,at==24 || at==40 ? 4u : 2u);
        check(!decode_impact_wav(wav),"Unsupported or truncated replacement WAVs are rejected");
    }
    wav=saved; wav.pop_back();
    check(!decode_impact_wav(wav),"A truncated RIFF payload cannot become a playback buffer");
    wav=saved; wav.resize(192046); put(4,192038,4); put(40,192002,4);
    check(!decode_impact_wav(wav),"Replacement clips longer than two seconds are rejected");
    wav=saved;
    const std::array<std::byte,10> junk{std::byte{'J'},std::byte{'U'},std::byte{'N'},std::byte{'K'},std::byte{1},
        std::byte{},std::byte{},std::byte{},std::byte{7},std::byte{}};
    wav.insert(wav.begin()+12,junk.begin(),junk.end()); put(4,54,4);
    check(decode_impact_wav(wav)==samples,"Optional odd-sized RIFF chunks are skipped with their padding");
}
std::vector<std::byte> test_wav(unsigned rate,const std::vector<std::int16_t>& pcm) {
    std::vector<std::byte> wav(44+pcm.size()*2);
    const auto put=[&](std::size_t at,unsigned value,unsigned count) {
        for (unsigned i=0;i<count;++i) wav[at+i]=static_cast<std::byte>((value>>(i*8))&255);
    };
    std::memcpy(wav.data(),"RIFF",4); put(4,static_cast<unsigned>(wav.size()-8),4);
    std::memcpy(wav.data()+8,"WAVEfmt ",8); put(16,16,4);
    put(20,1,2); put(22,1,2); put(24,rate,4); put(28,rate*2,4); put(32,2,2); put(34,16,2);
    std::memcpy(wav.data()+36,"data",4); put(40,static_cast<unsigned>(pcm.size()*2),4);
    std::memcpy(wav.data()+44,pcm.data(),pcm.size()*2);
    return wav;
}
void custom_audio_sample_rates() {
    constexpr double pi=3.14159265358979323846;
    for (const unsigned rate : {8000u,11025u,22050u,44100u,48000u,96000u,192000u}) {
        std::vector<std::int16_t> tone(rate/10);
        for (std::size_t i=0;i<tone.size();++i)
            tone[i]=static_cast<std::int16_t>(std::lround(16000*std::sin(2*pi*1000*static_cast<double>(i)/rate)));
        std::string_view error="Previous failure";
        const auto decoded=decode_impact_wav(test_wav(rate,tone),&error);
        check(decoded && decoded->size()==(tone.size()*48000ull+rate-1)/rate && error.empty(),
            "Lower and higher sample rates preserve duration to within one 48 kHz sample");
        if (decoded && decoded->size()>192) {
            double squared_error{};
            for (std::size_t i=96;i<decoded->size()-96;++i) {
                const double ideal=16000*std::sin(2*pi*1000*static_cast<double>(i)/48000);
                squared_error+=std::pow((*decoded)[i]-ideal,2);
            }
            check(std::sqrt(squared_error/static_cast<double>(decoded->size()-192))<35,
                "Resampling preserves a 1 kHz tone's pitch, timing and amplitude");
        }
        if (rate==48000) check(decoded && *decoded==tone,"48 kHz clips remain sample-identical");
        const auto constant=decode_impact_wav(test_wav(rate,std::vector<std::int16_t>(3,12345)));
        check(constant && std::all_of(constant->begin(),constant->end(),[](auto v) {return v==12345;}),
            "Very short clips retain constant gain without invalid boundary reads");
        const auto full=decode_impact_wav(test_wav(rate,std::vector<std::int16_t>(rate*2,-32768)));
        check(full && full->size()==96000 && full->front()==-32768 && full->back()==-32768,
            "The two-second limit uses input time and resampling stays within PCM16 bounds");
        check(!decode_impact_wav(test_wav(rate,std::vector<std::int16_t>(rate*2+1)),&error) &&
            error=="Sound is longer than two seconds.","One input sample over two seconds is rejected with a useful error");
    }
    // Frequencies above the output Nyquist limit must be filtered rather than
    // folded into audible tones when a high-rate recording is downsampled.
    for (const unsigned rate : {96000u,192000u}) {
        std::vector<std::int16_t> tone(rate/10);
        for (std::size_t i=0;i<tone.size();++i)
            tone[i]=static_cast<std::int16_t>(std::lround(16000*std::sin(2*pi*32000*static_cast<double>(i)/rate)));
        const auto decoded=decode_impact_wav(test_wav(rate,tone));
        double energy{};
        if (decoded) for (std::size_t i=96;i<decoded->size()-96;++i) energy+=static_cast<double>((*decoded)[i])*(*decoded)[i];
        check(decoded && std::sqrt(energy/static_cast<double>(decoded->size()-192))<10,
            "Downsampling suppresses out-of-band audio instead of introducing alias tones");
    }
    std::string_view error;
    for (const unsigned rate : {0u,7999u,192001u})
        check(!decode_impact_wav(test_wav(rate,{123}),&error) && error=="Supported sample rates are 8-192 kHz.",
            "Rates outside the supported range fail with a specific error");
    auto bad=test_wav(44100,{123}); bad[28]=std::byte{};
    check(!decode_impact_wav(bad,&error) && error=="Invalid PCM byte rate or block alignment.",
        "An inconsistent PCM header cannot reach conversion");
    bad=test_wav(11025,{123}); bad[22]=std::byte{2};
    check(!decode_impact_wav(bad,&error) && error=="Use a mono WAV file.","Stereo WAVs report the mono requirement");
    bad=test_wav(11025,{123}); bad[20]=std::byte{3};
    check(!decode_impact_wav(bad,&error) && error=="Use a 16-bit PCM WAV file.","Other encodings report the PCM16 requirement");
    check(!decode_impact_wav(std::vector<std::byte>(max_impact_wav_bytes+1),&error) &&
        error=="WAV file exceeds the 1 MiB limit.","File allocation remains bounded even for high sample rates");
    auto data_first=test_wav(11025,{123,456});
    std::rotate(data_first.begin()+12,data_first.begin()+36,data_first.end());
    check(decode_impact_wav(data_first)==decode_impact_wav(test_wav(11025,{123,456})),
        "WAV chunk order does not affect format validation or resampling");
}
void player_zoom_and_pass_out() {
    const ZoomImpulse zoom{1000,1,.3f,.5f};
    const auto start=sample_player_zoom(zoom,1000),hold=sample_player_zoom(zoom,1250),end=sample_player_zoom(zoom,1900);
    check(start.active && start.fov_scale==1 && start.focus_weight==0,"Player zoom begins without a camera jump");
    check(std::abs(hold.fov_scale-.7f)<.001f && hold.focus_weight==1 && end.fov_scale>hold.fov_scale && end.focus_weight<1,
        "Player zoom holds on the skater then eases back with slow-motion recovery");
    check(!sample_player_zoom(zoom,2000).active && !sample_player_zoom(zoom,999).active &&
        !sample_player_zoom({1000,1,std::numeric_limits<float>::quiet_NaN(),.5f},1200).active,
        "Expired or invalid zoom impulses leave the native view unchanged");
    const ZoomImpulse gentle{1000,.85f,.3f,.45f};
    check(sample_player_zoom(gentle,1020).fov_scale>.99f && sample_player_zoom(gentle,1080).focus_weight<.2f,
        "The default zoom eases in over a quarter second instead of reaching full strength in eighty milliseconds");
    float previous_fov=1;
    for (std::uint64_t now=1000;now<=1850;++now) {
        const auto frame=sample_player_zoom(gentle,now);
        check(frame.fov_scale>=.7f && frame.fov_scale<=1 && frame.focus_weight>=0 && frame.focus_weight<=1 &&
            std::abs(frame.fov_scale-previous_fov)<.003f,"Zoom enters and exits continuously without overshooting camera bounds");
        previous_fov=frame.fov_scale;
    }
    VisualOptions options; options.pass_out_seconds=2;
    VisualEvents event; event.latest_impact_ms=1000; event.latest_severity=500;
    PassOutPulse fade;
    check(fade.step(options,event,1000,true,true).darkness==0,"Pass-out starts transparently");
    const auto peak=fade.step(options,event,1600,true,true);
    check(peak.darkness>0 && peak.vignette==options.pass_out_strength && peak.hud_fade==peak.vignette,
        "A severe fall darkens the screen edges and fades the score interface together");
    options.pass_out_redness=.8f;
    const auto red=fade.step(options,event,1600,true,true);
    check(red.redness==.8f && red.darkness==peak.darkness && red.hud_fade==peak.hud_fade,
        "The red tint slider applies immediately without changing fade timing or strength");
    options.pass_out_redness=0;
    check(fade.step(options,event,1600,true,true).redness==0,"Zero redness restores the black fade");
    options.pass_out_hud=false;
    check(fade.step(options,event,1700,true,true).hud_fade==0,"The score HUD can stay visible independently");
    event.latest_impact_ms=1900;
    check(fade.step(options,event,2700,true,true).darkness<peak.darkness && fade.step(options,event,3000,true,true).darkness==0,
        "Further impacts do not prolong the pass-out envelope");
    event.latest_impact_ms=3300;
    check(fade.step(options,event,3600,true,true).darkness==0,"A completed fade cannot restart during the same fall");
    fade.step(options,event,4000,true,false); event.latest_impact_ms=4200;
    fade.step(options,event,4200,true,true);
    check(fade.step(options,event,4600,true,true).darkness>0,"Recovery rearms the next fall's fade");
    check(fade.step(options,event,4650,false,true).darkness==0 && fade.step(options,event,4700,true,true).darkness==0,
        "Menu or focus interruption clears the fade without replaying it");
    fade.step(options,event,5000,true,false); event.latest_impact_ms=5200; options.reduced_effects=true;
    check(fade.step(options,event,5200,true,true).darkness==0,"Reduced effects suppresses pass-out presentation");
    options.reduced_effects=false; event.latest_impact_ms=5400;
    check(fade.step(options,event,5700,true,true).darkness==0,"Enabling effects mid-fall cannot replay suppressed pass-out");
    fade.step(options,event,6000,true,false); event.latest_impact_ms=6200; event.latest_severity=50;
    check(fade.step(options,event,6300,true,true).darkness==0,"Minor hits do not cause pass-out");
    event.latest_impact_ms=6400; event.latest_fracture=true;
    fade.step(options,event,6400,true,true);
    check(fade.step(options,event,6800,true,true).darkness>0 && fade.step(options,event,6810,true,false).darkness==0,
        "A newly fractured bone qualifies, while native recovery immediately clears the fade");
}
void smooth_player_follow() {
    PlayerCameraFollow follow;
    check(follow.step({0,0,0},1000,.12f)==Vec3{},"Follow starts at the current player position");
    const auto first=follow.step({1,0,0},1016,.12f);
    check(first[0]>0 && first[0]<.1f,"A physics position jump becomes a gentle camera acceleration");
    check(follow.step({1,0,0},1016,.12f)==first,"Repeated render passes at the same time cannot advance smoothing twice");
    float previous=first[0];
    for (std::uint64_t now=1032;now<=1608;now+=16) {
        const auto next=follow.step({1,0,0},now,.12f);
        check(next[0]>=previous && next[0]<=1 && next[1]==0 && next[2]==0,
            "Tracking continues between physics updates and settles without oscillation or overshoot");
        previous=next[0];
    }
    check(previous>.999f,"Damped tracking converges to the player's position");
    PlayerCameraFollow coarse,fine;
    coarse.step({},1000,.12f); fine.step({},1000,.12f);
    const auto at30=coarse.step({1,2,3},1200,.12f);
    Vec3 at120{};
    for (std::uint64_t now=1005;now<=1200;now+=5) at120=fine.step({1,2,3},now,.12f);
    for (unsigned axis=0;axis<3;++axis)
        check(std::abs(at30[axis]-at120[axis])<.00001f,"Follow response is independent of render frame rate");
    check(follow.step({20,0,0},1624,.12f)==Vec3{20,0,0},"A teleport clears camera momentum rather than tracking across the map");
    check(follow.step({21,0,0},2000,.12f)==Vec3{21,0,0},"A stalled render clock resets the follow filter");
    check(follow.step({22,0,0},1900,.12f)==Vec3{22,0,0},"A reversed clock cannot integrate old camera velocity");
    follow.reset();
    check(follow.step({5,6,7},2100,.12f)==Vec3{5,6,7},"A fresh fall cannot inherit camera momentum");
    check(follow.step({std::numeric_limits<float>::infinity(),0,0},2110,.12f)==Vec3{} &&
        follow.step({8,9,10},2120,.12f)==Vec3{8,9,10},"Invalid tracking inputs reset safely");
    PlayerCameraFollow fast,slow;
    fast.step({},1000,.04f); slow.step({},1000,.4f);
    check(fast.step({1,0,0},1100,.04f)[0]>slow.step({1,0,0},1100,.4f)[0],"The follow smoothing slider changes tracking responsiveness");
}
void visibility_and_flashes() {
    VisualOptions options;
    VisualEvents events;
    Result result; result.phase=Phase::attempt;
    events.observe(result,1000);
    check(visual_appearance(options,result,events,1000,false).opacity==options.opacity,"Always shows the skeleton before impacts");
    options.visibility=XrayVisibility::impact;
    check(visual_appearance(options,result,events,1000,false).opacity==0,"The skeleton stays hidden while skating without impacts");
    result.phase=Phase::bailed;
    check(visual_appearance(options,result,events,1000,false).opacity==0,"Bailing alone cannot reveal the skeleton without an impact");
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
    options.visibility=XrayVisibility::always; options.only_impacted=true;
    const auto always=visual_appearance(options,result,events,3500,false);
    check(always.opacity==options.opacity && always.colors[102][3]==options.opacity && always.colors[7][3]==0,
        "Always retains impacted bones after the fade duration and still respects the bone filter");
    options.visibility=XrayVisibility::impact; options.only_impacted=false;
    result.impacts=2; result.injuries[2].severity=300; result.injuries[2].fractured=true;
    result.bone_injuries[277]={300,0,true};
    events.observe(result,4000);
    options.reduced_effects=true;
    const auto reduced=visual_appearance(options,result,events,4000,false);
    check(reduced.opacity==options.opacity,"A new impact refreshes visibility after the previous fade has expired");
    check(reduced.colors[277][0]==1 && reduced.colors[277][1]==.16f && reduced.colors[277][2]==.22f,
        "Reduced effects retains the steady fracture color and removes transient tints");
    options.only_fractured=true;
    for (const bool normal_play : {false,true}) {
        const auto fractures=visual_appearance(options,result,events,4000,false,normal_play);
        check(fractures.colors[277][3]==options.opacity && fractures.colors[277][0]==1 &&
            fractures.colors[102][3]==0 && fractures.colors[7][3]==0,
            "Fracture-only X-ray retains red broken bones and hides bruised and uninjured bones in attempts and normal play");
    }
    options.only_impacted=true;
    check(visual_appearance(options,result,events,4000,false).colors[102][3]==0,
        "Fracture-only display takes priority over the saved impacted-bones option");
    options.only_fractured=false; options.only_impacted=false;
    check(visual_appearance(options,result,events,4000,false).colors[102][3]==options.opacity,
        "Disabling fracture-only display immediately restores bruised bones");
    check(events.impacts_at_ms[102]==2000 && events.impacts_at_ms[277]==4000,"A second bone keeps its own impact time");
    check(visual_appearance(options,result,events,4000,true).opacity==0,"First person hides the mesh before it can cover the camera");
    options.visibility=XrayVisibility::off;
    check(visual_appearance(options,result,events,4000,false).opacity==0 && events.latest_impact_ms==4000,
        "Off hides the skeleton while preserving injury events for other Slam effects");
    options.visibility=XrayVisibility::impact;
    result.cancelled=true; events.observe(result,4500);
    check(!events.latest_impact_ms && visual_appearance(options,result,events,4500,false).opacity==0,
        "Stopping, unloading or losing the skater clears effects");
    result.cancelled=false; result.phase=Phase::attempt; result.impacts=0; result.injuries={}; result.bone_injuries={};
    events.observe(result,5000);
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
int main(int argc,char** argv) {
    saved_options(); visibility_and_flashes(); normal_play_falls(); normal_play_recovery_fade(); impact_feedback(); configurable_bone_damage(); delayed_bail_feedback(); time_scale_ownership(); slow_motion_envelope(); impact_camera_timing(); custom_audio(); player_zoom_and_pass_out(); smooth_player_follow();
    custom_audio_sample_rates();
    // Optional local clips let developers exercise the actual decoder without
    // adding user recordings or an audio device dependency to the test suite.
    for (int i=1;i<argc;++i) {
        std::ifstream file(argv[i],std::ios::binary);
        const std::vector<char> input((std::istreambuf_iterator<char>(file)),{});
        std::string_view error;
        const auto decoded=decode_impact_wav(std::as_bytes(std::span(input)),&error);
        check(decoded.has_value(),"The supplied local WAV can be loaded and converted");
        if (decoded) std::cout<<argv[i]<<": "<<decoded->size()<<" samples at 48 kHz.\n";
        else std::cerr<<argv[i]<<": "<<error<<'\n';
    }
    if (failures) return 1;
    std::cout<<"Slam visual timing, accessibility and saved-options checks passed.\n";
}
