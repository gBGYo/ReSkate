#include "impact_model.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace dingosdk::blood {
namespace {
constexpr float native_impact_gap_s=.2f;
constexpr float minimum_impact_speed=3.f;
float length(const Vec3& v) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }
bool finite(const Vec3& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x) && std::abs(x) < 1000000; });
}
Vec3 cross(const Vec3& a,const Vec3& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
float dot(const Vec3& a,const Vec3& b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool rigid_pose(const std::array<float,16>& pose) {
    const Vec3 x{pose[0],pose[1],pose[2]},y{pose[4],pose[5],pose[6]},z{pose[8],pose[9],pose[10]};
    return finite(x) && finite(y) && finite(z) && finite({pose[12],pose[13],pose[14]}) &&
        std::abs(dot(x,x)-1)<.05f && std::abs(dot(y,y)-1)<.05f && std::abs(dot(z,z)-1)<.05f &&
        std::abs(dot(x,y))<.05f && std::abs(dot(x,z))<.05f && std::abs(dot(y,z))<.05f && dot(cross(x,y),z)>.95f;
}
std::optional<Vec3> angular_velocity(const Body& before,const Body& body,float dt) {
    if (!before.pose_valid || !body.pose_valid || !rigid_pose(before.pose) || !rigid_pose(body.pose)) return {};
    Vec3 angular{};
    float trace{};
    for (std::size_t offset : {0u,4u,8u}) {
        const Vec3 a{before.pose[offset],before.pose[offset+1],before.pose[offset+2]};
        const Vec3 b{body.pose[offset],body.pose[offset+1],body.pose[offset+2]};
        const auto product=cross(a,b);
        for (std::size_t axis=0;axis<3;++axis) angular[axis]+=product[axis]/(2*dt);
        trace+=dot(a,b);
    }
    // sum(cross(old_axis,new_axis))/2dt estimates world angular velocity.
    // Large intersample turns are ambiguous: use linear telemetry instead.
    return trace>=2.f && finite(angular) && length(angular)<=100 ? std::optional(angular) : std::nullopt;
}
std::optional<Vec3> velocity_at_contact(const Body& body,const Vec3& angular,const Vec3& point) {
    Vec3 lever{};
    for (std::size_t axis=0;axis<3;++axis) lever[axis]=point[axis]-body.pose[12+axis];
    if (!finite(lever) || length(lever)>1.5f) return {};
    auto velocity=cross(angular,lever);
    for (std::size_t axis=0;axis<3;++axis) velocity[axis]+=body.velocity[axis];
    return finite(velocity) && length(velocity)<=300 ? std::optional(velocity) : std::nullopt;
}
bool usable(const Frame& frame) {
    if (!frame.valid || !frame.entity || !frame.world || !finite(frame.center) ||
        !frame.body_count || frame.body_count > max_bodies) return false;
    for (std::size_t i = 0; i < frame.body_count; ++i) {
        const auto& body = frame.bodies[i];
        if (body.region >= Region::count || body.joint< -1 || body.joint>=static_cast<int>(injury_bone_count) ||
            body.injury_joint< -1 || body.injury_joint>=static_cast<int>(injury_bone_count) ||
            !finite(body.velocity) || length(body.velocity) > 300 ||
            (body.speed_valid && (!std::isfinite(body.contact_speed) || body.contact_speed<0 || body.contact_speed>300)) ||
            (body.contact && body.normal_valid && (!finite(body.normal) || length(body.normal) < .5f || length(body.normal) > 1.5f)))
            return false;
    }
    return true;
}
}
void ImpactTracker::reset() noexcept {
    // Preserve serial uniqueness across falls for the persistent blood model.
    const auto serial=next_episode_;
    *this=ImpactTracker{}; next_episode_=serial;
}
bool ImpactTracker::begin(const Frame& frame) {
    if (!usable(frame) || frame.bailed) return false;
    reset(); active_=true; previous_=frame; previous_valid_=true;
    for (std::size_t i=0;i<frame.body_count;++i) {
        contacts_[i].blocked=frame.bodies[i].contact;
        contacts_[i].remaining=native_impact_gap_s;
    }
    return true;
}
void ImpactTracker::step(const Frame& frame) {
    impact_count_=0;
    if (!active_) return;
    if (frame.entity!=previous_.entity || frame.world!=previous_.world) {reset(); return;}
    if (!std::isfinite(frame.dt) || frame.dt<=0 || frame.dt>.1f || !usable(frame)) {
        previous_valid_=false; recent_impacts_={}; previous_angular_valid_={}; return;
    }
    if (bailed_ && !frame.bailed) {begin(frame); return;}
    for (std::size_t i=0;i<frame.body_count;++i) {
        auto& recent=recent_impacts_[i];
        recent.remaining=std::max(0.f,recent.remaining-frame.dt);
        if (!recent.remaining) recent={};
        if (!previous_valid_ || previous_.body_count!=frame.body_count) {
            contacts_[i]={};
            contacts_[i].blocked=frame.bodies[i].contact;
            contacts_[i].remaining=native_impact_gap_s;
            recent={};
        } else if (frame.bodies[i].joint!=previous_.bodies[i].joint ||
            frame.bodies[i].injury_joint!=previous_.bodies[i].injury_joint || frame.bodies[i].region!=previous_.bodies[i].region) {
            contacts_[i]={};
            contacts_[i].blocked=frame.bodies[i].contact;
            contacts_[i].remaining=native_impact_gap_s;
            recent={};
        }
        auto& episode=contacts_[i];
        const auto& body=frame.bodies[i];
        const bool native_detail=body.contact && body.normal_valid && body.speed_valid;
        const bool native_hit=native_detail && body.contact_speed>=minimum_impact_speed && !body.hit.board_only();
        if (!body.contact || (native_detail && !native_hit)) {
            episode.remaining=std::max(0.f,episode.remaining-frame.dt);
            if ((!episode.native && !native_detail) || episode.remaining<=.00001f) episode={};
        } else if (native_hit || !episode.native) {
            episode.remaining=native_impact_gap_s;
        }
        // Missing detail is unknown, not evidence of separation. A held raw
        // flag with an unreadable normal must not rearm an existing episode.
        episode.native=episode.native || native_detail;
    }
    Vec3 displacement{};
    float speed{};
    for (std::size_t i = 0; i < frame.body_count; ++i) speed = std::max(speed, length(frame.bodies[i].velocity));
    std::array<Vec3,max_bodies> angular{};
    std::array<bool,max_bodies> angular_valid{};
    if (previous_valid_ && previous_.bailed && frame.bailed && previous_.body_count==frame.body_count) {
        for (std::size_t i=0;i<frame.body_count;++i) {
            const auto& body=frame.bodies[i];
            const auto& before=previous_.bodies[i];
            if (body.joint!=before.joint || body.injury_joint!=before.injury_joint) continue;
            if (const auto estimate=angular_velocity(before,body,frame.dt)) {
                angular[i]=*estimate; angular_valid[i]=true;
            }
        }
    }
    if (previous_valid_) {
        for (std::size_t i = 0; i < 3; ++i) displacement[i] = frame.center[i] - previous_.center[i];
        if (length(displacement) > std::max(2.f, speed * frame.dt * 3.f)) {
            reset();
            return;
        }
    }
    const bool entering_bail=!bailed_ && frame.bailed;
    std::array<float,max_bodies> body_changes{};
    std::array<ImpactContact,max_bodies> physical_changes{};
    if (previous_valid_ && previous_.body_count==frame.body_count &&
        active_) {
        for (std::size_t i=0;i<frame.body_count;++i) {
            const auto& body=frame.bodies[i];
            const auto& before=previous_.bodies[i];
            auto& episode=contacts_[i];
            if (!body.contact || !body.normal_valid || episode.blocked || body.hit.board_only() || body.region!=before.region ||
                body.joint!=before.joint || body.injury_joint!=before.injury_joint) continue;
            const float n=length(body.normal);
            const auto impact_change=[&](const Vec3& incoming,const Vec3& outgoing,bool gravity=true) {
                float change{},approaching{};
                for (std::size_t axis=0;axis<3;++axis) {
                    const float normal=body.normal[axis]/n;
                    const float free_velocity=incoming[axis]-(gravity && axis==1 ? 9.81f*frame.dt : 0);
                    change+=(outgoing[axis]-free_velocity)*normal;
                    approaching-=free_velocity*normal;
                }
                return approaching>=2 && change>=3 ? change : 0.f;
            };
            // The native producer measures relative contact speed before
            // response. Prefer it to animation/solver velocity changes, even
            // when the body was already touching. Only a higher episode peak
            // can add damage. Estimates remain an onset-only fallback.
            float change=body.speed_valid ? body.contact_speed :
                episode.speed==0 ? impact_change(before.velocity,body.velocity) : 0.f;
            if (episode.speed==0 && body.point_valid && finite(body.contact_point) &&
                angular_valid[i] && previous_angular_valid_[i]) {
                auto incoming_body=before, outgoing_body=body;
                // The producer's normal speed uses body linear velocities.
                // Keep independently verified rotation at a contact point,
                // without reintroducing animation's linear delta or gravity.
                if (body.speed_valid) incoming_body.velocity=outgoing_body.velocity=Vec3{};
                const auto incoming=velocity_at_contact(incoming_body,previous_angular_[i],body.contact_point);
                const auto outgoing=velocity_at_contact(outgoing_body,angular[i],body.contact_point);
                if (incoming && outgoing) change=std::max(change,impact_change(*incoming,*outgoing,!body.speed_valid));
            }
            if (change<minimum_impact_speed || change<=episode.speed) continue;
            episode.speed=change;
            episode.remaining=native_impact_gap_s;
            ImpactContact physical;
            physical.body=body; physical.incoming_velocity=before.velocity;
            physical.body_index=i; physical.speed=change;
            physical.severity=std::min(change,60.f)*std::min(change,60.f);
            if (!bailed_ && !frame.bailed) {
                if (change>recent_impacts_[i].change) recent_impacts_[i]={change,.2f,physical};
            }
            else {
                body_changes[i]=std::max(body_changes[i],change);
                physical_changes[i]=physical;
            }
        }
    }
    if (entering_bail) {
        for (std::size_t i=0;i<frame.body_count;++i) {
            if (recent_impacts_[i].remaining>0) {
                body_changes[i]=std::max(body_changes[i],recent_impacts_[i].change);
                if (recent_impacts_[i].physical.speed>physical_changes[i].speed)
                    physical_changes[i]=recent_impacts_[i].physical;
            }
            recent_impacts_[i]={};
        }
    }
    bailed_=bailed_ || frame.bailed;
    if (bailed_ && previous_valid_ && previous_.body_count==frame.body_count) {
        for (std::size_t i=0;i<frame.body_count;++i) {
            const float change=body_changes[i];
            if (change<=0 || physical_changes[i].speed<=0) continue;
            const float severity=std::min(change,60.f)*std::min(change,60.f);
            auto& episode=contacts_[i];
            if (severity<=episode.severity) continue;
            auto& physical=physical_changes[i];
            physical.new_episode=episode.severity==0;
            if (physical.new_episode) episode.serial=++next_episode_;
            physical.episode=episode.serial;
            impacts_[impact_count_++]=physical;
            episode.severity=severity; episode.speed=std::max(episode.speed,change);
        }
    }
    previous_=frame; previous_valid_=true;
    previous_angular_=angular; previous_angular_valid_=angular_valid;
}
}
