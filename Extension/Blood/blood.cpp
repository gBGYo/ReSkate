#include "blood.h"
#include "blood_response.h"
#include <algorithm>
#include <cmath>

namespace dingosdk::blood {
namespace {
bool finite(const Vec3& p) {return std::all_of(p.begin(),p.end(),[](float v){return std::isfinite(v) && std::abs(v)<1000000;});}
Vec3 cross(const Vec3& a,const Vec3& b) {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
float dot(const Vec3& a,const Vec3& b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vec3 normalized(Vec3 v) {
    const auto n=std::sqrt(dot(v,v));
    if (!finite(v) || n<.0001f) return {0,1,0};
    for (auto& x:v) x/=n;
    return v;
}
bool pose(const Body& b) {
    if (!b.pose_valid || !finite({b.pose[12],b.pose[13],b.pose[14]})) return false;
    for (unsigned i=0;i<3;++i) {
        const Vec3 axis{b.pose[4*i],b.pose[4*i+1],b.pose[4*i+2]};
        if (!finite(axis) || std::abs(dot(axis,axis)-1)>.15f) return false;
        for (unsigned j=0;j<i;++j)
            if (std::abs(dot(axis,{b.pose[4*j],b.pose[4*j+1],b.pose[4*j+2]}))>.1f) return false;
    }
    return true;
}
std::array<float,16> transform(const Vec3& at,Vec3 up,float scale) {
    up=normalized(up);
    const auto right=normalized(cross(std::abs(up[1])<.9f ? Vec3{0,1,0} : Vec3{0,0,1},up));
    const auto forward=cross(right,up);
    return {right[0]*scale,right[1]*scale,right[2]*scale,0,
        up[0]*scale,up[1]*scale,up[2]*scale,0,
        forward[0]*scale,forward[1]*scale,forward[2]*scale,0,at[0],at[1],at[2],1};
}
Vec3 local_point(const Body& b,const Vec3& at) {
    Vec3 delta{at[0]-b.pose[12],at[1]-b.pose[13],at[2]-b.pose[14]},out{};
    for (unsigned i=0;i<3;++i) out[i]=dot(delta,{b.pose[4*i],b.pose[4*i+1],b.pose[4*i+2]});
    return out;
}
Vec3 world_point(const Body& b,const Vec3& at) {
    Vec3 out{b.pose[12],b.pose[13],b.pose[14]};
    for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j) out[j]+=b.pose[4*i+j]*at[i];
    return out;
}
Vec3 world_direction(const Body& b,const Vec3& direction) {
    Vec3 out{};
    for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j) out[j]+=b.pose[4*i+j]*direction[i];
    return normalized(out);
}
}
void BloodModel::clear() noexcept {
    // Never reuse logical handles, even when scoring starts a new attempt.
    scene_={}; ground_.clear(); episodes_={}; bailed_=false;
}
void BloodModel::suspend() noexcept {
    scene_.sources={}; episodes_={}; bailed_=false; ground_.suspend();
}
void BloodModel::step(const Frame& f,std::span<const ImpactContact> impacts,
    const BloodOptions& options,std::uint64_t generation) noexcept {
    if (!valid_blood_tuning(options.tuning) || !options.enabled || !std::isfinite(options.strength) || options.strength<=0 || options.strength>1 ||
        !std::isfinite(options.minimum_damage) || options.minimum_damage<0 || options.minimum_damage>3600) {clear(); return;}
    if (!f.valid || !f.entity || !f.world || f.body_count>max_bodies ||
        !std::isfinite(f.dt) || f.dt<=0 || f.dt>.1f) {suspend(); return;}
    if (scene_.entity!=f.entity || scene_.world!=f.world || scene_.generation!=generation || scene_.color!=options.tuning.color) clear();
    scene_.color=options.tuning.color;
    scene_.entity=f.entity; scene_.world=f.world; scene_.generation=generation;
    ground_.step(f,impacts,options.enabled,options.reduced,options.strength,generation,options.minimum_damage,options.tuning);
    scene_.ground=ground_.scene();
    if (!f.bailed) {episodes_={}; bailed_=false;}
    else if (!bailed_) {episodes_={}; bailed_=true;}
    const std::size_t limit=options.reduced ? 6 : max_blood_sources;
    for (std::size_t index=0;index<scene_.sources.size();++index) {
        auto& source=scene_.sources[index];
        if (index>=limit) {source={}; continue;}
        if (!source.id) continue;
        source.age+=f.dt;
        if (source.age>=source.lifetime) {source={}; continue;}
        source.emitting=f.bailed && source.age<source.emit_seconds;
        if (options.reduced && source.kind!=BloodKind::spray) {source.emitting=false; source.emit_seconds=source.age;}
        if (source.kind==BloodKind::spray || !source.emitting) continue;
        const Body* body=nullptr;
        for (std::size_t i=0;i<f.body_count;++i) if (f.bodies[i].joint==source.joint) {body=&f.bodies[i]; break;}
        if (!body || !pose(*body)) {source.emitting=false; source.emit_seconds=source.age; continue;}
        const auto at=world_point(*body,source.local_point);
        const Vec3 previous{source.transform[12],source.transform[13],source.transform[14]};
        Vec3 delta{at[0]-previous[0],at[1]-previous[1],at[2]-previous[2]};
        if (!finite(at) || dot(delta,delta)>4) {source.emitting=false; source.emit_seconds=source.age; continue;}
        const auto scale=std::sqrt(source.transform[4]*source.transform[4]+source.transform[5]*source.transform[5]+source.transform[6]*source.transform[6]);
        source.transform=transform(at,world_direction(*body,source.local_direction),scale);
    }
    if (!f.bailed) return;
    unsigned bursts{};
    // Spend the two-wound budget on the hardest contacts, not skeleton order.
    std::array<const ImpactContact*,max_bodies> ordered{};
    std::size_t hit_count{};
    for (const auto& hit:impacts) if (std::isfinite(hit.severity) && hit_count<ordered.size()) ordered[hit_count++]=&hit;
    std::sort(ordered.begin(),ordered.begin()+hit_count,[](const auto* a,const auto* b){
        return a->severity!=b->severity ? a->severity>b->severity : a->body_index<b->body_index;
    });
    for (std::size_t index=0;index<hit_count;++index) {
        const auto& hit=*ordered[index];
        if (!hit.episode || hit.body_index>=max_bodies ||
            episodes_[hit.body_index]==hit.episode || !std::isfinite(hit.severity) || hit.severity<=0 || hit.severity<options.minimum_damage ||
            !hit.body.normal_valid || !finite(hit.body.normal) || dot(hit.body.normal,hit.body.normal)<.1f ||
            hit.body.hit.board_only()) continue;
        // A contact can cross the threshold as its impact develops. Consume
        // it only once it qualifies, then suppress further peak updates.
        episodes_[hit.body_index]=hit.episode;
        const bool has_pose=pose(hit.body);
        if ((!hit.body.point_valid || !finite(hit.body.contact_point)) && !has_pose) {++scene_.dropped; continue;}
        if (++bursts>2u) {++scene_.dropped; continue;}
        const auto wounds=std::count_if(scene_.sources.begin(),scene_.sources.end(),[](const auto& s){
            return s.id && s.kind==BloodKind::trail && s.emitting;
        });
        const bool already_bleeding=std::any_of(scene_.sources.begin(),scene_.sources.end(),[&](const auto& s){
            return s.id && s.kind==BloodKind::trail && s.emitting && s.joint==hit.body.joint;
        });
        if (already_bleeding || wounds>=2) {++scene_.dropped; continue;}
        Vec3 at=hit.body.point_valid && finite(hit.body.contact_point) ? hit.body.contact_point :
            Vec3{hit.body.pose[12],hit.body.pose[13],hit.body.pose[14]};
        const auto normal=normalized(hit.body.normal);
        for (unsigned i=0;i<3;++i) at[i]+=.015f*normal[i];
        // Orient the donor's +Y launch axis away from the surface, tilted in
        // the incoming tangential direction. Droplets then fall under gravity.
        Vec3 direction=normal;
        if (finite(hit.incoming_velocity)) {
            const auto speed=dot(hit.incoming_velocity,normal);
            for (unsigned i=0;i<3;++i) direction[i]+=.035f*(hit.incoming_velocity[i]-speed*normal[i]);
        }
        const auto response=blood_impact_response(hit.severity);
        // The size preference remains a global multiplier. Small hits reduce
        // emission rather than making centimeter-sized drops disappear.
        const float scale=.3f+.9f*options.strength;
        bool spawned{};
        for (const auto kind:{BloodKind::spray,BloodKind::droplets,BloodKind::trail}) {
            if (kind!=BloodKind::spray && (!has_pose || hit.body.joint<0 || options.reduced)) continue;
            auto slot=std::find_if(scene_.sources.begin(),scene_.sources.begin()+limit,[](const auto& s){return !s.id;});
            if (slot==scene_.sources.begin()+limit) {++scene_.dropped; break;}
            BloodSource source;
            source.id=++next_id_; source.kind=kind;
            source.intensity=blood_intensity(hit.severity);
            if (options.reduced && source.intensity==BloodIntensity::heavy) source.intensity=BloodIntensity::medium;
            source.transform=transform(at,direction,scale*(kind==BloodKind::spray ? response.spray_scale : response.drop_scale));
            source.joint=hit.body.joint; source.local_point=has_pose ? local_point(hit.body,at) : Vec3{};
            if (has_pose) for (unsigned i=0;i<3;++i)
                source.local_direction[i]=dot(normalized(direction),{hit.body.pose[4*i],hit.body.pose[4*i+1],hit.body.pose[4*i+2]});
            // Brief wound emission leaves independent falling drops as the
            // skater tumbles, without connecting distant source positions.
            // Spray is an instantaneous burst; attached emitters add more
            // droplets over time as damage increases.
            source.emit_seconds=kind==BloodKind::spray ? .12f : (kind==BloodKind::droplets ? .5f : .7f)*response.emission;
            source.lifetime=source.emit_seconds+(kind==BloodKind::droplets ? .8f : .5f); source.emitting=true;
            *slot=source; spawned=true;
        }
        if (spawned) ++scene_.accepted;
    }
}
}
