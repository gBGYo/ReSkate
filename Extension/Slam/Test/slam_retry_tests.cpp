#include "Extension/Slam/slam_retry.h"
#include <iostream>
#include <limits>

namespace {
using namespace dingosdk;
using namespace dingosdk::slam;
unsigned failures{};
void check(bool value,const char* message) {if (!value) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}}
SavedStart start() {
    SavedStart saved;
    saved.owner={1,11,22,33,44,55,66}; saved.client=77; saved.generation=9; saved.map="levels/retry-test";
    saved.transform={0,0,-1,0, 0,1,0,0, 1,0,0,0, 20,3,-40,1};
    saved.config.kind=ChallengeKind::fractures; saved.config.target=2;
    return saved;
}
RetryObservation observation(const SavedStart& saved) {
    RetryObservation f;
    f.owner=saved.owner; f.client=saved.client; f.entity=saved.owner.entity; f.world=saved.owner.world; f.map=saved.map;
    f.generation=saved.generation; f.now=1000; f.sample_at=999; f.transform=saved.transform;
    f.allowed=f.owned=f.fresh=f.pose_valid=true;
    return f;
}
void poses() {
    const auto saved=start();
    check(valid_start_transform(saved.transform),"A rotated affine start is accepted without replacing its orientation");
    for (unsigned i=0;i<16;++i) {
        auto bad=saved.transform; bad[i]=std::numeric_limits<float>::quiet_NaN();
        check(!valid_start_transform(bad),"Nonfinite lanes are rejected");
    }
    auto bad=saved.transform; bad[13]=1000001;
    check(!valid_start_transform(bad),"World coordinate bounds are enforced");
    bad=saved.transform; bad[4]=.5f;
    check(!valid_start_transform(bad),"A sheared transform cannot enter native teleport");
    bad=saved.transform; bad[8]=-1;
    check(!valid_start_transform(bad),"A mirrored transform cannot enter native teleport");
    bad=saved.transform; bad[2]=-2;
    check(!valid_start_transform(bad),"A scaled transform cannot enter native teleport");
    bad=saved.transform; bad[15]=0;
    check(!valid_start_transform(bad),"The homogeneous translation lane must be affine");
    SavedStartRetry retry; auto invalid=saved; invalid.config.target=.5f;
    check(!retry.save(invalid),"Invalid challenge rules cannot be saved for retry");
    invalid=saved; invalid.map.assign(513,'x');
    check(!retry.save(invalid),"Oversized map identities are rejected");
    invalid=saved; invalid.owner.entity=0;
    check(!retry.save(invalid),"A saved start needs a real owner");
}
void arrival() {
    const auto saved=start(); auto f=observation(saved); SavedStartRetry retry;
    check(retry.save(saved) && retry.available(f) && retry.begin(f),"A recovered matching skater can retry");
    check(!retry.begin(f) && !retry.save(saved),"Starting or replacing an in-flight retry is rejected");
    check(retry.step(f)==RetryStep::dispatch,"Dispatch is requested only before native submission");
    retry.submitted({1,99,7},1001); f.now=1100;
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::waiting,"An idle native manager and pre-teleport physics cannot finish retry");
    f.sample_at=1090; f.transform[12]+=4;
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::waiting,"Idle native completion at the wrong location cannot start scoring");
    f.transform=saved.transform; f.transform[0]=1; f.transform[2]=0; f.transform[8]=0; f.transform[10]=1;
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::waiting,"Returning to the right position with the wrong facing is rejected");
    f.transform=saved.transform;
    check(retry.step(f,SkaterTeleportState::busy)==RetryStep::waiting,"Pose coincidence while native teleport is busy cannot complete retry");
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::waiting,"The first matching fresh sample cannot complete retry");
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::waiting,"Repeating the same physics sample cannot complete retry");
    f.now=1120; f.sample_at=1110;
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::arrived && !retry.active(),"Two distinct fresh matching samples finish the same native request");
    check(retry.saved()->transform==saved.transform && retry.saved()->config==saved.config,
        "Retry preserves the original location, orientation and immutable rules");
    check(retry.available(f) && retry.begin(f),"The saved start supports a second attempt without being recaptured at the fall");
    retry.cancel();
    check(!retry.active() && retry.saved().has_value(),"Cancel retry clears the pending handoff and retains the start");
    f.bailed=true; check(!retry.begin(f),"A ragdolled skater must recover before retrying");
    f.bailed=false; f.fresh=false; check(!retry.begin(f),"Stale telemetry cannot authorize retry");
}
void invalidation() {
    const auto saved=start();
    for (unsigned change=0;change<8;++change) {
        SavedStartRetry retry; auto f=observation(saved); retry.save(saved); retry.begin(f);
        retry.submitted({1,99,7},1001); f.now=1020;
        switch (change) {
        case 0: f.map="levels/other"; break;
        case 1: ++f.generation; break;
        case 2: ++f.client; break;
        case 3: ++f.entity; break;
        case 4: ++f.owner.entity; break;
        case 5: ++f.owner.world; break;
        case 6: f.allowed=false; break;
        case 7: ++f.world; break;
        }
        check(retry.step(f)==RetryStep::failed && !retry.active() && !retry.saved(),
            "Map reload, owner changes and mode conflicts invalidate the saved start and pending handoff");
    }
    for (auto native:{SkaterTeleportState::interrupted,SkaterTeleportState::unavailable}) {
        SavedStartRetry retry; auto f=observation(saved); retry.save(saved); retry.begin(f); retry.submitted({1,99,7},1001);
        check(retry.step(f,native)==RetryStep::failed && !retry.active(),"A superseded native request cannot start an attempt");
    }
    SavedStartRetry retry; auto f=observation(saved); retry.save(saved); retry.begin(f);
    f.now=6000; check(retry.step(f)==RetryStep::failed,"The busy dispatch queue expires in five seconds");
    f.now=7000; retry.begin(f); retry.submitted({1,99,8},7001); f.now=9000; f.owned=f.fresh=f.pose_valid=false;
    f.entity=0;
    check(retry.step(f,SkaterTeleportState::busy)==RetryStep::waiting,"Temporary ownership loss while this native teleport runs cannot dispatch again");
    check(retry.retains_start(f),"The submitted return survives the debug model's zero entity only in the same world and request");
    f.owned=true;
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::failed,"A fresh owned zero/different entity is not borrowed by a returning retry");
    f=observation(saved); f.now=7000; retry.save(saved); retry.begin(f); retry.submitted({1,99,8},7001);
    f.now=9000; f.entity=0; f.owned=f.fresh=f.pose_valid=false;
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::waiting,"Idle native completion alone cannot turn a missing skater into arrival");
    f.now=17001;
    check(retry.step(f)==RetryStep::failed && !retry.active(),"Missing post-teleport physics expires after ten seconds");
}
void rebuilt_physics() {
    const auto saved=start(); auto f=observation(saved); SavedStartRetry retry; retry.save(saved); retry.begin(f);
    auto other=f; ++other.owner.rig;
    check(retry.step(other)==RetryStep::failed && !retry.saved(),"Before dispatch a replaced physics owner invalidates retry");
    retry.save(saved); retry.begin(f); retry.submitted({1,99,7},1001);
    f.now=1050; f.entity=0; f.owned=f.fresh=f.pose_valid=false;
    check(retry.step(f,SkaterTeleportState::busy)==RetryStep::waiting,"The submitted native return survives its missing-entity interval");
    f=observation(saved); f.now=1100; f.sample_at=1090; ++f.owner.core; ++f.owner.context; ++f.owner.rig; ++f.owner.selector;
    check(retry.retains_start(f) && retry.step(f,SkaterTeleportState::idle)==RetryStep::waiting,
        "Rebuilt physics for the same validated entity needs two fresh arrival samples");
    f.sample_at=1101; ++f.owner.rig;
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::waiting,"Two arrival samples from different physics owners cannot be combined");
    f.now=1120; f.sample_at=1110;
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::arrived && retry.saved()->owner==f.owner,
        "Only the stable rebuilt owner at the original pose is adopted after this same native request completes");
    check(retry.saved()->transform==saved.transform && retry.saved()->config==saved.config && retry.available(f),
        "A second retry uses the rebuilt owner and retains the original transform and challenge");
    check(!retry.retains_start(observation(saved)),"Retired physics cannot be reused after successful owner handoff");
}
void multiplayer_permissions() {
    const auto saved=start(); auto f=observation(saved); SavedStartRetry retry;
    set_multiplayer_session_active(true);
    set_session_tools_allowed(false,true,true);
    check(retry.save(saved) && !retry.available(f) && !retry.begin(f),
        "Host teleport restrictions block saved-start retry even with a fresh owned skater");
    set_session_tools_allowed(true,true,true);
    check(retry.available(f) && retry.begin(f) && retry.step(f)==RetryStep::dispatch,
        "Host permission allows multiplayer retry for the verified local skater");
    set_session_tools_allowed(false,true,true);
    check(retry.step(f)==RetryStep::failed && !retry.active() && retry.saved(),
        "Revoking teleport permission cancels a queued retry before native dispatch and retains its saved start");
    set_session_tools_allowed(true,true,true);
    check(retry.begin(f),"Retry becomes available when host permission returns");
    retry.submitted({1,99,7},1001);
    set_session_tools_allowed(false,true,true);
    check(retry.step(f,SkaterTeleportState::idle)==RetryStep::failed && !retry.active(),
        "Revoking permission after dispatch cannot start an attempt from native completion");
    set_session_tools_allowed(true,true,true);
    set_multiplayer_session_active(false);
}
}
int main() {
    poses(); arrival(); invalidation(); rebuilt_physics(); multiplayer_permissions();
    if (failures) return 1;
    std::cout<<"Saved-start retry ownership, pose and completion checks passed.\n";
}
