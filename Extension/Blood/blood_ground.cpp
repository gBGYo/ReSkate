#include "blood_ground.h"
#include "blood_response.h"
#include "blood_surface.h"
#include <algorithm>
#include <cmath>

namespace dingosdk::blood {
namespace {
bool finite(Vec3 v) {return std::all_of(v.begin(),v.end(),[](float x){return std::isfinite(x) && std::abs(x)<100000;});}
Vec3 sub(Vec3 a,Vec3 b) {return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
float dot(Vec3 a,Vec3 b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vec3 mul(Vec3 a,float n) {for (auto& x:a) x*=n; return a;}
Vec3 cross(Vec3 a,Vec3 b) {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Vec3 unit(Vec3 v) {return mul(v,1/std::sqrt(dot(v,v)));}
Vec3 tangent(Vec3 direction,Vec3 normal) {
    direction=sub(direction,mul(normal,dot(direction,normal)));
    if (!finite(direction) || dot(direction,direction)<.0001f) {
        const auto reference=std::abs(normal[0])<.8f ? Vec3{1,0,0} : Vec3{0,0,1};
        direction=sub(reference,mul(normal,dot(reference,normal)));
    }
    return unit(direction);
}
std::size_t cell_hash(int x,int y,int z) {return ((std::uint32_t(x)*73856093u)^(std::uint32_t(y)*83492791u)^(std::uint32_t(z)*19349663u))&4095u;}
Vec3 axis(const BloodMark& mark,unsigned offset) {const auto& t=mark.transform; return {t[offset],t[offset+1],t[offset+2]};}
Vec3 receiver(const BloodMark& mark) {
    return sub(axis(mark,12),mul(unit(axis(mark,4)),mark.projection_lift));
}
float radius(const BloodMark& mark) {return .5f*std::sqrt(dot(axis(mark,0),axis(mark,0))+dot(axis(mark,8),axis(mark,8)));}
bool overlaps(const BloodMark& a,const BloodMark& b) {
    const auto normal=unit(axis(a,4)),up=unit(axis(b,4)),delta=sub(receiver(a),receiver(b));
    if (dot(normal,up)<.95f || std::abs(dot(delta,normal))>.025f || std::abs(dot(delta,up))>.025f) return false;
    const auto ax=axis(a,0),az=axis(a,8),bx=axis(b,0),bz=axis(b,8);
    // Compare the inner 75% of both oriented footprints. Texture borders can
    // touch without making a patch dense; broad and rotated smears still count.
    for (const auto direction:{unit(ax),unit(az),unit(bx),unit(bz)})
        if (std::abs(dot(delta,direction))>=.375f*(std::abs(dot(ax,direction))+std::abs(dot(az,direction))+
            std::abs(dot(bx,direction))+std::abs(dot(bz,direction)))) return false;
    return true;
}
float random_unit(std::uint32_t& state) {
    state^=state<<13; state^=state>>17; state^=state<<5;
    return float(state>>8)*(1.f/16777216);
}
bool surface(const Body& b) {
    if (!b.contact || !b.normal_valid || !b.point_valid || !b.pose_valid || b.joint<0 ||
        b.hit.board_only() || !finite(b.normal) || !finite(b.contact_point)) return false;
    const auto n=dot(b.normal,b.normal);
    const Vec3 at{b.pose[12],b.pose[13],b.pose[14]};
    return n>.25f && n<2.25f && finite(at) &&
        dot(sub(at,b.contact_point),sub(at,b.contact_point))<1;
}
// Native torso proxies cover the spine and hips. Combine their actual surface
// contacts into one footprint instead of painting parallel lines per joint.
bool torso_contact(const Frame& f,Body& footprint) {
    const Body* support=nullptr;
    for (std::size_t i=0;i<f.body_count;++i) {
        const auto& body=f.bodies[i];
        if (body.region!=Region::torso || !surface(body)) continue;
        if (!support || (support->joint==7 && body.joint!=7)) support=&body;
    }
    if (!support) return false;
    const auto normal=unit(support->normal);
    Vec3 point{},position{},velocity{}; float weight{};
    for (std::size_t i=0;i<f.body_count;++i) {
        const auto& body=f.bodies[i];
        if (body.region!=Region::torso || !surface(body) || body.hit.vehicle!=support->hit.vehicle || dot(unit(body.normal),normal)<.9f) continue;
        const auto separation=sub(body.contact_point,support->contact_point);
        // Do not average different steps, walls or distant receivers.
        if (std::abs(dot(separation,normal))>.06f || dot(separation,separation)>.81f) continue;
        const Vec3 at{body.pose[12],body.pose[13],body.pose[14]};
        auto offset=sub(at,body.contact_point);
        offset=sub(offset,mul(normal,dot(offset,normal)));
        const float tangent=std::sqrt(dot(offset,offset));
        if (tangent>.3f) offset=mul(offset,.3f/tangent);
        const float contribution=body.joint==7 ? .6f : 1.f;
        for (unsigned axis=0;axis<3;++axis) {
            point[axis]+=(body.contact_point[axis]+offset[axis])*contribution;
            position[axis]+=at[axis]*contribution;
            velocity[axis]+=body.velocity[axis]*contribution;
        }
        weight+=contribution;
    }
    footprint=*support; footprint.normal=normal;
    footprint.contact_point=mul(point,1/weight); footprint.velocity=mul(velocity,1/weight);
    for (unsigned axis=0;axis<3;++axis) footprint.pose[12+axis]=position[axis]/weight;
    return true;
}

}
float BloodMark::opacity() const noexcept {
    const float alpha=std::clamp((lifetime-age)/blood_mark_fade_seconds,0.f,1.f);
    return retirement<0 ? alpha : std::min(alpha,std::clamp(retirement/blood_pressure_fade_seconds,0.f,1.f));
}
void BloodGroundModel::clear() noexcept {
    scene_={}; wounds_={}; episodes_={}; next_slot_=active_=retiring_=0; focus_={};
    cells_={}; cell_entries_={}; largest_radius_=0;
}
void BloodGroundModel::index(std::size_t slot) noexcept {
    const auto& mark=scene_.marks[slot]; const auto point=receiver(mark);
    auto& entry=cell_entries_[slot]; entry.x=int(std::floor(point[0])); entry.y=int(std::floor(point[1])); entry.z=int(std::floor(point[2]));
    auto& head=cells_[cell_hash(entry.x,entry.y,entry.z)];
    entry.previous=0; entry.next=head;
    if (head) cell_entries_[head-1].previous=std::uint16_t(slot+1);
    head=std::uint16_t(slot+1);
    // A conservative bound until clear(), including after the largest expires.
    largest_radius_=std::max(largest_radius_,radius(mark));
}
void BloodGroundModel::unindex(std::size_t slot) noexcept {
    auto& entry=cell_entries_[slot];
    if (entry.previous) cell_entries_[entry.previous-1].next=entry.next;
    else cells_[cell_hash(entry.x,entry.y,entry.z)]=entry.next;
    if (entry.next) cell_entries_[entry.next-1].previous=entry.previous;
    entry={};
}
bool BloodGroundModel::crowded(const BloodMark& candidate) noexcept {
    // Include the receiver-plane tolerance for nearly coplanar sloped surfaces.
    const auto point=receiver(candidate); const float reach=radius(candidate)+largest_radius_+.025f;
    unsigned neighbours{};
    for (int x=int(std::floor(point[0]-reach));x<=int(std::floor(point[0]+reach));++x)
      for (int y=int(std::floor(point[1]-reach));y<=int(std::floor(point[1]+reach));++y)
        for (int z=int(std::floor(point[2]-reach));z<=int(std::floor(point[2]+reach));++z)
            for (auto link=cells_[cell_hash(x,y,z)];link;link=cell_entries_[link-1].next) {
                const auto& entry=cell_entries_[link-1];
                ++scene_.stats.neighbour_checks;
                if (entry.x!=x || entry.y!=y || entry.z!=z) continue; // Hash collisions are not spatial neighbours.
                const auto& mark=scene_.marks[link-1];
                if (mark.opacity()<.5f || (candidate.impact && !mark.impact)) continue;
                if ((candidate.kind==BloodMarkKind::body)!=(mark.kind==BloodMarkKind::body) ||
                    (candidate.kind==BloodMarkKind::drop)!=(mark.kind==BloodMarkKind::drop)) continue;
                if (overlaps(candidate,mark) && ++neighbours>=3) return true;
            }
    return false;
}
void BloodGroundModel::retire() noexcept {
    if (active_-retiring_<blood_mark_soft_limit) return;
    BloodMark* victim=nullptr; float best=-1;
    for (auto& mark:scene_.marks) {
        // Recent impacts must not vanish in a contact storm. If everything is
        // fresh, admission is throttled until there is room to fade a victim.
        if (!mark.id || mark.retirement>=0 || mark.age<(mark.impact ? 10.f : 5.f)) continue;
        const auto& t=mark.transform;
        const auto delta=sub(Vec3{t[12],t[13],t[14]},focus_);
        const float distance=dot(delta,delta);
        const float score=(finite(focus_) && distance>400 ? 1000.f : 0.f)+
            (mark.impact ? 0.f : mark.kind==BloodMarkKind::drop ? 300.f : mark.kind==BloodMarkKind::body ? 100.f : 200.f)+mark.age;
        if (score>best) {best=score; victim=&mark;}
    }
    if (victim) {victim->retirement=blood_pressure_fade_seconds; ++retiring_; ++scene_.stats.retirement_started;}
}
void BloodGroundModel::stamp(Vec3 point,Vec3 normal,Vec3 direction,float width,float length,BloodMarkKind kind,unsigned body_variant,bool impact,bool vehicle) noexcept {
    // Decorative drops are the first thing to stop adding under pressure.
    if (kind==BloodMarkKind::drop && active_>=max_blood_marks*3/4) {++scene_.stats.pressure_rejected; return;}
    const auto right=tangent(direction,normal),forward=cross(right,normal);
    // The projection box needs headroom above the receiver at grazing views.
    // A 7 cm box centred on the physics contact can become occluded/clipped
    // against the rendered ground. Keep the contact inside a deeper box;
    // changing local Y and its centre does not enlarge the painted X/Z area.
    const bool broad=kind==BloodMarkKind::body;
    const bool shallow=vehicle || normal[1]<=.5f;
    const float projection_depth=shallow ? .08f : broad ? .55f : .24f,projection_lift=shallow ? .015f : broad ? .12f : .04f;
    constexpr float footprint_scale=1.15f;
    const auto x=mul(right,length*footprint_scale),y=mul(normal,projection_depth),z=mul(forward,width*footprint_scale);
    for (unsigned i=0;i<3;++i) point[i]+=normal[i]*projection_lift;
    BloodMark mark; mark.kind=kind; mark.body_variant=static_cast<std::uint8_t>(body_variant); mark.impact=impact;
    mark.lifetime=tuning_.lifetime; mark.vehicle=vehicle; mark.projection_lift=projection_lift;
    mark.transform={x[0],x[1],x[2],0,y[0],y[1],y[2],0,z[0],z[1],z[2],0,point[0],point[1],point[2],1};
    if (crowded(mark)) {++scene_.stats.density_rejected; return;}
    retire();
    if (active_>=(impact ? max_blood_marks : max_blood_marks-blood_impact_reserve)) {++scene_.stats.pressure_rejected; return;}
    while (scene_.marks[next_slot_].id) next_slot_=(next_slot_+1)%max_blood_marks;
    mark.id=++next_id_;
    scene_.marks[next_slot_]=mark; index(next_slot_); next_slot_=(next_slot_+1)%max_blood_marks;
    ++active_; ++scene_.stats.spawned;
}
void BloodGroundModel::deposit(Wound& wound,Vec3 point,Vec3 normal,Vec3 direction,float width,bool initial,bool reduced,unsigned& budget) noexcept {
    // Consume a fixed random packet per distance sample, even when the frame
    // budget is exhausted. Sampling rate must not change the trail pattern.
    std::array<float,10> r; for (auto& value:r) value=random_unit(wound.random);
    const bool stretched=wound.torso && !initial && r[1]<.42f;
    const float length=wound.torso ? (stretched ? .36f+.19f*r[6] : .16f+.16f*r[6]) : initial ? .13f+.08f*r[6] : .09f+.21f*r[6];
    // Unequal scuffs and longer narrow pulls interrupt the stamp cadence.
    // Occasionally overlap the next scuff, then leave larger dry gaps. The
    // overall spacing stays sparse without reading as regularly spaced prints.
    wound.spacing=(wound.torso ? (.14f+.6f*length+.14f*r[0])*(r[0]<.3f ? .5f : 1.f)+(r[8]<.16f ? .14f : 0) : .055f+.105f*r[0]+(r[1]<.16f ? .11f : 0))*(reduced ? 1.8f : 1.f)/tuning_.density;
    if (wound.torso) {
        const auto choice=unsigned(r[4]*(wound.body_variant<3 ? 2 : 3));
        wound.body_variant=wound.body_variant<3 ? (wound.body_variant+1+choice)%3 : choice;
    }
    direction=tangent(direction,normal); const auto side=cross(direction,normal);
    // Half turns vary asymmetric silhouettes without mirroring the projection
    // box or reversing its face winding. Both orientations follow the drag.
    const float angle=(r[2]-.5f)*.7f+(wound.torso && r[9]<.5f ? 3.14159265f : 0.f);
    const float offset=(r[3]-.5f)*(wound.torso ? .075f : .05f);
    Vec3 axis=direction;
    for (unsigned i=0;i<3;++i) {axis[i]=direction[i]*std::cos(angle)+side[i]*std::sin(angle); point[i]+=side[i]*offset;}
    const auto kind=wound.torso ? BloodMarkKind::body : BloodMarkKind(std::min(2u,unsigned(r[4]*3)));
    if (budget) {
        const auto response=blood_impact_response(wound.severity);
        // Enlarge the contact footprint in its plane, retaining the verified
        // projection depth. Subsequent drag marks grow more gently.
        const float scale=initial ? response.stain_scale : response.trail_scale;
        const float breadth=width*(wound.torso ? (stretched ? .6f+.35f*r[5] : .8f+.35f*r[5]) : .5f+r[5]);
        stamp(point,normal,axis,breadth*scale*tuning_.width,length*scale*tuning_.length,kind,wound.torso ? wound.body_variant : 0,initial,wound.vehicle);
        --budget;
    }
    if (!initial && !reduced && r[7]<(wound.torso ? .2f : .6f) && budget) {
        const float lateral=(r[8]<.5f ? -1.f : 1.f)*(wound.torso ? width*(.28f+.14f*r[9]) : .035f+.075f*r[9]);
        for (unsigned i=0;i<3;++i) point[i]+=side[i]*lateral-direction[i]*.025f;
        const float size=.025f+.04f*r[8];
        stamp(point,normal,side,size,size*(.8f+.4f*r[9]),BloodMarkKind::drop,0,false,wound.vehicle); --budget;
    }
}
void BloodGroundModel::step(const Frame& f,std::span<const ImpactContact> impacts,bool enabled,bool reduced,float strength,std::uint64_t generation,float minimum_damage,const BloodTuning& tuning) noexcept {
    if (!valid_blood_tuning(tuning) || !enabled || !std::isfinite(strength) || strength<=0 || strength>1 ||
        !std::isfinite(minimum_damage) || minimum_damage<0 || minimum_damage>3600) {clear(); return;}
    if (!f.valid || !f.entity || !f.world || f.body_count>max_bodies ||
        !std::isfinite(f.dt) || f.dt<=0 || f.dt>.1f) {suspend(); return;}
    if (scene_.entity!=f.entity || scene_.world!=f.world || scene_.generation!=generation || scene_.color!=tuning.color) clear();
    tuning_=tuning; scene_.color=tuning.color;
    scene_.entity=f.entity; scene_.world=f.world; scene_.generation=generation;
    focus_=f.center; active_=retiring_=0;
    for (std::size_t slot=0;slot<scene_.marks.size();++slot) if (auto& mark=scene_.marks[slot];mark.id) {
        mark.age+=f.dt;
        if (mark.retirement>=0) {
            mark.retirement=std::max(0.f,mark.retirement-f.dt);
            if (mark.retirement==0) {unindex(slot); mark={}; ++scene_.stats.retired; continue;}
        }
        if (mark.age>=mark.lifetime) {unindex(slot); mark={}; ++scene_.stats.expired; continue;}
        ++active_; retiring_+=mark.retirement>=0;
    }
    if (!f.bailed) {wounds_={}; episodes_={}; return;}
    for (auto& wound:wounds_) {wound.remaining-=f.dt; if (wound.remaining<=0) wound={};}
    for (const auto& hit:impacts) {
        if (!hit.episode || hit.body_index>=max_bodies || episodes_[hit.body_index]==hit.episode ||
            !std::isfinite(hit.severity) || hit.severity<=0 || hit.severity<minimum_damage || hit.body.joint<0 || hit.body.hit.board_only()) continue;
        episodes_[hit.body_index]=hit.episode;
        const bool torso=hit.body.region==Region::torso;
        auto wound=std::find_if(wounds_.begin(),wounds_.end(),[&](const auto& w){
            return w.joint>=0 && (torso ? w.torso : !w.torso && w.joint==hit.body.joint);
        });
        if (wound==wounds_.end()) wound=std::find_if(wounds_.begin(),wounds_.end(),[](const auto& w){return w.joint<0;});
        if (wound!=wounds_.end()) {
            if (wound->joint<0) {
                wound->random=std::uint32_t(hit.episode*0x9e3779b9ULL)^std::uint32_t(hit.body.joint*0x85ebca6bULL)^std::uint32_t(f.entity)^std::uint32_t(f.world);
                if (!wound->random) wound->random=1;
            }
            if (wound->joint<0) wound->joint=hit.body.joint;
            // Several torso contacts share a wound; a weaker later contact
            // must not shrink the stronger injury or shorten its bleeding.
            wound->severity=std::max(wound->severity,hit.severity);
            wound->torso=torso;
            wound->remaining=std::max(wound->remaining,blood_impact_response(hit.severity).bleed_seconds*tuning_.bleeding);
            wound->pending_impact=true;
        }
    }
    unsigned budget=reduced ? 2 : 6;
    // Fresh impacts precede trails, with the torso first within each group.
    // Snapshot the classification so a newly tracked wound is visited once.
    std::array<bool,max_bodies> initial{};
    for (std::size_t i=0;i<wounds_.size();++i) initial[i]=!wounds_[i].tracking || wounds_[i].pending_impact;
    for (bool impact_pass:{true,false}) for (bool torso_pass:{true,false}) for (std::size_t wound_index=0;wound_index<wounds_.size();++wound_index) {
        auto& wound=wounds_[wound_index];
        if (wound.joint<0 || wound.torso!=torso_pass || initial[wound_index]!=impact_pass) continue;
        const Body* body=nullptr; Body footprint;
        if (wound.torso) {if (torso_contact(f,footprint)) body=&footprint;}
        else for (std::size_t i=0;i<f.body_count;++i) if (f.bodies[i].joint==wound.joint) {body=&f.bodies[i]; break;}
        if (!body || !surface(*body)) {wound.tracking=false; wound.distance=0; continue;}
        if (wound.tracking && wound.vehicle!=body->hit.vehicle) {wound.tracking=false; initial[wound_index]=true;}
        wound.vehicle=body->hit.vehicle;
        const auto point=body->contact_point,normal=unit(body->normal);
        const Vec3 at{body->pose[12],body->pose[13],body->pose[14]};
        const auto delta=sub(point,wound.point),body_delta=sub(at,wound.body_position);
        const float distance=std::sqrt(dot(delta,delta));
        if (wound.tracking && dot(body_delta,body_delta)>2.25f) {wound={}; continue;}
        const float anatomy=body->region==Region::head ? 1.1f : .8f;
        const float width=wound.torso ? .40f+.08f*strength : (.09f+.055f*strength)*anatomy;
        if (initial[wound_index]) {
            if (!budget) continue; // Keep the impact pending for the next valid contact sample.
            deposit(wound,point,normal,body->velocity,width,true,reduced,budget);
            wound.distance=0; wound.pending_impact=false;
        } else if (distance>.7f || dot(normal,wound.normal)<.85f || std::abs(dot(delta,normal))>.035f+distance*.04f) {
            // Lift-offs, stairs and separate planes start a fresh path.
            wound.distance=0;
        } else if (distance>.00001f) {
            const auto direction=mul(delta,1/distance);
            float travel=wound.spacing-wound.distance;
            while (travel<=distance) {
                Vec3 sample=wound.point;
                for (unsigned i=0;i<3;++i) sample[i]+=direction[i]*travel;
                deposit(wound,sample,normal,direction,width,false,reduced,budget);
                travel+=wound.spacing;
            }
            // Carry distance, never queue old positions after a frame budget.
            wound.distance=std::max(0.f,wound.spacing-(travel-distance));
        }
        wound.point=point; wound.normal=normal; wound.body_position=at; wound.tracking=true;
    }
}
void BloodGroundPool::clear(const BloodGroundFunctions& f) {
    for (auto& entry:entries_) {if (entry.handle) f.release(f.context,entry.handle); entry={};}
    entity_=world_=generation_=0;
}
std::size_t BloodGroundPool::active() const noexcept {
    return std::count_if(entries_.begin(),entries_.end(),[](const auto& e){return e.handle!=0;});
}
std::size_t BloodGroundPool::failed() const noexcept {
    return std::count_if(entries_.begin(),entries_.end(),[](const auto& e){return e.failed;});
}
std::size_t BloodGroundPool::attached() const noexcept {
    return std::count_if(entries_.begin(),entries_.end(),[](const auto& e){return e.handle && e.receiver[0];});
}
void BloodGroundPool::update(const BloodGroundScene& scene,const BloodGroundFunctions& f,bool allowed) {
    if (!f.create || !f.release) return;
    if (!allowed || entity_!=scene.entity || world_!=scene.world || generation_!=scene.generation) clear(f);
    if (!allowed || !scene.entity || !scene.world) return;
    entity_=scene.entity; world_=scene.world; generation_=scene.generation;
    // Drain obsolete handles first, including slots later in the array. A
    // snapshot can contain several removals and additions in arbitrary slots.
    for (std::size_t i=0;i<entries_.size();++i) {
        auto& entry=entries_[i];
        if (entry.id!=scene.marks[i].id) {
            if (entry.handle) f.release(f.context,entry.handle);
            entry={};
        }
    }
    for (std::size_t i=0;i<entries_.size();++i) {
        auto& entry=entries_[i]; const auto& mark=scene.marks[i];
        if (entry.id!=mark.id) {
            entry={mark.id,0};
            // Missing materials or a suspended client must not replay old marks.
            if (mark.id && mark.age<1) {
                if (f.bind) {
                    BloodReceiver receiver;
                    // Never leave an unbound stain floating where a car/prop was.
                    if (!f.pose || !f.move || !f.bind(f.context,mark,receiver) ||
                        !receiver.id[0] || !blood_surface::valid_pose(receiver.pose)) {
                        entry.failed=true; continue;
                    }
                    entry.receiver=receiver.id;
                    entry.local=blood_surface::local(receiver.pose,mark.transform);
                    entry.drawn=mark.transform;
                }
                entry.handle=f.create(f.context,mark);
                entry.failed=!entry.handle;
            }
        }
        if (entry.handle && entry.receiver[0]) {
            BloodMatrix pose;
            if (!f.pose(f.context,entry.receiver,pose) || !blood_surface::valid_pose(pose)) {
                // Keep the logical id consumed; deletion or handle recycling
                // must not recreate this mark on a different object.
                f.release(f.context,entry.handle); entry.handle=0; continue;
            }
            const auto transform=blood_surface::world(pose,entry.local);
            if (blood_surface::changed(transform,entry.drawn)) {
                f.move(f.context,entry.handle,transform); entry.drawn=transform;
            }
        }
        if (entry.handle && f.opacity) {
            // Quantize to keep tiny time steps from flooding the native queue.
            const auto alpha=static_cast<std::uint8_t>(std::lround(mark.opacity()*255));
            if (entry.opacity!=alpha) {f.opacity(f.context,entry.handle,float(alpha)/255); entry.opacity=alpha;}
        }
    }
}
}
