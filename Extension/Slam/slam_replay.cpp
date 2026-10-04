#include "slam_replay.h"
#include <algorithm>
#include <cmath>

namespace dingosdk::slam {
namespace {
bool same_visuals(const ReplayVisualFrame& a,const ReplayVisualFrame& b) {
    if (a.result.phase!=b.result.phase || a.result.cancelled!=b.result.cancelled || a.result.config!=b.result.config ||
        a.events.impacts_at_ms!=b.events.impacts_at_ms || a.events.fractures_at_ms!=b.events.fractures_at_ms ||
        a.events.latest_impact_ms!=b.events.latest_impact_ms) return false;
    for (std::size_t i=0;i<injury_bone_count;++i) {
        const auto& x=a.result.bone_injuries[i]; const auto& y=b.result.bone_injuries[i];
        if (x.severity!=y.severity || x.fractured!=y.fractured) return false;
    }
    return true;
}
}
std::uint64_t ReplayVisualHistory::milliseconds(double time) noexcept {
    // Zero is reserved by VisualEvents for an event that never happened.
    return std::isfinite(time) && time>=0 && time<=1e9 ? static_cast<std::uint64_t>(time*1000)+1 : 0;
}
void ReplayVisualHistory::reset() noexcept {frames_.clear(); source_events_.reset();}
void ReplayVisualHistory::record(double time,const Result& result,const VisualEvents& events,std::uint64_t wall_now) {
    const auto now=milliseconds(time);
    if (!now) return;
    if (!frames_.empty() && time<frames_.back().until) reset();
    ReplayVisualFrame next{result,events,events.fractures_at_ms};
    const auto previous=frames_.empty() ? nullptr : &frames_.back().frame.events;
    const auto stamp=[&](std::uint64_t event,std::uint64_t source,std::uint64_t mapped) {
        if (!event) return std::uint64_t{};
        if (previous && event==source) return mapped;
        const auto age=wall_now>=event ? wall_now-event : 0;
        return now>age ? now-age : std::uint64_t{1};
    };
    for (std::size_t i=0;i<injury_bone_count;++i) {
        next.events.impacts_at_ms[i]=stamp(events.impacts_at_ms[i],source_events_.impacts_at_ms[i],previous ? previous->impacts_at_ms[i] : 0);
        next.events.fractures_at_ms[i]=stamp(events.fractures_at_ms[i],source_events_.fractures_at_ms[i],previous ? previous->fractures_at_ms[i] : 0);
    }
    next.events.latest_impact_ms=stamp(events.latest_impact_ms,source_events_.latest_impact_ms,previous ? previous->latest_impact_ms : 0);
    source_events_=events;
    const bool continuous=!frames_.empty() && time-frames_.back().until<=coverage_gap_seconds;
    const bool unchanged=continuous && same_visuals(frames_.back().frame,next);
    if (continuous) frames_.back().until=time;
    if (!unchanged) frames_.push_back({time,time,std::move(next)});
    while (frames_.size()>capacity || (frames_.size()>1 && time-frames_.front().until>retention_seconds)) frames_.pop_front();
    frames_.front().time=std::max(frames_.front().time,time-retention_seconds);
}
std::optional<ReplayVisualFrame> ReplayVisualHistory::sample(double time) const {
    if (!milliseconds(time)) return {};
    const auto after=std::upper_bound(frames_.begin(),frames_.end(),time,[](double t,const Entry& entry) {return t<entry.time;});
    if (after==frames_.begin()) return {};
    const auto& entry=*std::prev(after);
    // Small endpoint tolerance allows presentation between adjacent ticks.
    if (time>entry.until+.05) return {};
    return entry.frame;
}
}
