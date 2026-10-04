#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace dingosdk::replay_export {
// Native capture slots, rather than elapsed time or an assumed GPU latency,
// identify the pose/camera/injury state belonging to an encoded frame.
struct CaptureKey {
    std::uintptr_t manager{},encoder{},lease{},ring{},resource{};
    std::uint32_t slot{};
    std::array<std::byte,48> metadata{};
    bool operator==(const CaptureKey&) const = default;
};
template<class Frame> class CaptureQueue {
    struct Entry {CaptureKey key; Frame frame;};
    std::array<std::optional<Entry>,64> slots_;
public:
    // The caller serializes native copy/encode operations with its own mutex.
    void clear() noexcept {for (auto& slot : slots_) slot.reset();}
    void record(const CaptureKey& key,Frame frame) {
        if (key.slot<slots_.size()) slots_[key.slot]=Entry{key,std::move(frame)};
    }
    std::optional<Frame> take(const CaptureKey& key) {
        if (key.slot>=slots_.size()) return {};
        auto& slot=slots_[key.slot];
        if (!slot) return {};
        std::optional<Frame> result;
        if (slot->key==key) result=std::move(slot->frame);
        slot.reset();
        return result;
    }
};
}
