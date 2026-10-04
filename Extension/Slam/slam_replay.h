#pragma once
#include "slam_visuals.h"
#include <deque>

namespace dingosdk::slam {
struct ReplayVisualFrame {
    Result result;
    VisualEvents events;
    // Keep the original fracture identifiers so crack geometry is identical
    // in gameplay and replay, even though effect timestamps use replay time.
    std::array<std::uint64_t,injury_bone_count> fracture_seeds{};
};
class ReplayVisualHistory {
public:
    static constexpr std::size_t capacity=2048;
    static constexpr double retention_seconds=600, coverage_gap_seconds=.25;
    void reset() noexcept;
    void record(double time,const Result& result,const VisualEvents& events,std::uint64_t wall_now=0);
    std::optional<ReplayVisualFrame> sample(double time) const;
    std::size_t size() const noexcept {return frames_.size();}
    static std::uint64_t milliseconds(double time) noexcept;
private:
    struct Entry {double time{},until{}; ReplayVisualFrame frame;};
    std::deque<Entry> frames_;
    VisualEvents source_events_;
};
}
