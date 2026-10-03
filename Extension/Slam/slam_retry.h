#pragma once
#include "slam_model.h"
#include "Extension/Skater/skater_teleport.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

namespace dingosdk::slam {
using StartTransform=std::array<float,16>;
// Native teleport takes an affine row-vector transform. Reject scaled,
// sheared, mirrored and nonfinite poses rather than inventing a facing.
inline bool valid_start_transform(const StartTransform& pose) noexcept {
    return valid_skater_teleport_transform(pose);
}
struct SavedStart {
    LocalBailOwner owner;
    std::uintptr_t client{};
    std::uint64_t generation{};
    std::string map;
    StartTransform transform{};
    Config config;
};
struct RetryObservation {
    LocalBailOwner owner;
    std::uintptr_t client{},entity{},world{};
    std::string_view map;
    std::uint64_t generation{},now{},sample_at{};
    StartTransform transform{};
    bool allowed{},owned{},fresh{},bailed{},pose_valid{};
};
enum class RetryStep { waiting, dispatch, arrived, failed };
class SavedStartRetry {
public:
    bool save(SavedStart start) {
        if (active() || !start.client || !start.owner.entity || !start.owner.world || !start.owner.core || !start.owner.rig ||
            start.map.empty() || start.map.size()>512 || start.map.find('\0')!=std::string::npos ||
            !valid_config(start.config) || !valid_start_transform(start.transform)) return false;
        start_=std::move(start); status_="Starting location and orientation saved."; return true;
    }
    bool available(const RetryObservation& frame) const {
        return start_ && !active() && same_scope(frame) && frame.allowed && frame.owned && frame.fresh && !frame.bailed && frame.owner==start_->owner;
    }
    bool retains_start(const RetryObservation& frame) const {
        return same_scope(frame) && (!frame.owned || frame.owner==start_->owner ||
            (returning() && frame.owner.base==start_->owner.base && frame.owner.entity==start_->owner.entity &&
                frame.owner.world==start_->owner.world));
    }
    bool begin(const RetryObservation& frame) {
        if (!available(frame)) return false;
        phase_=Phase::queued; until_=frame.now+5000; matched_at_=0;
        status_="Returning to the saved start..."; return true;
    }
    RetryStep step(const RetryObservation& frame,SkaterTeleportState native=SkaterTeleportState::busy) {
        if (!active()) return RetryStep::waiting;
        if (!retains_start(frame) || !frame.allowed) {
            invalidate("Retry canceled: the skater, map or mode changed."); return RetryStep::failed;
        }
        if (frame.now>=until_) { cancel("Retry timed out. Recover and try again."); return RetryStep::failed; }
        if (phase_==Phase::queued) {
            if (!frame.owned || !frame.fresh || frame.bailed) return RetryStep::waiting;
            return RetryStep::dispatch;
        }
        if (native==SkaterTeleportState::interrupted || native==SkaterTeleportState::unavailable) {
            cancel("Retry interrupted by another teleport or an unavailable scene."); return RetryStep::failed;
        }
        if (native!=SkaterTeleportState::idle || !frame.owned || !frame.fresh || frame.bailed ||
            !frame.pose_valid || frame.sample_at<=sent_at_ || !at_start(frame.transform)) {
            matched_at_=0; matched_owner_.reset(); return RetryStep::waiting;
        }
        if (!matched_at_ || !matched_owner_ || *matched_owner_!=frame.owner) {
            matched_at_=frame.sample_at; matched_owner_=frame.owner; return RetryStep::waiting;
        }
        if (frame.sample_at<=matched_at_) return RetryStep::waiting;
        // Native teleport rebuilds physics while retaining the bound entity.
        // Adopt only the fully resolved owner confirmed by two post-request
        // samples at the saved pose. Old native addresses are never written.
        start_->owner=frame.owner;
        phase_=Phase::idle; status_="Returned to the saved start."; return RetryStep::arrived;
    }
    void submitted(SkaterTeleportReceipt receipt,std::uint64_t now) {
        if (phase_!=Phase::queued || !receipt.manager) return;
        receipt_=receipt; phase_=Phase::returning; sent_at_=now; until_=now+10000; matched_at_=0; matched_owner_.reset();
    }
    void cancel(const char* reason="Retry canceled.") { phase_=Phase::idle; receipt_={}; matched_at_=0; matched_owner_.reset(); status_=reason; }
    void invalidate(const char* reason) { cancel(reason); start_.reset(); }
    bool active() const { return phase_!=Phase::idle; }
    bool returning() const { return phase_==Phase::returning; }
    const std::optional<SavedStart>& saved() const { return start_; }
    const SkaterTeleportReceipt& receipt() const { return receipt_; }
    const std::string& status() const { return status_; }
private:
    enum class Phase { idle, queued, returning };
    bool same_scope(const RetryObservation& frame) const {
        // The client debug model briefly has no entity while our same-world
        // native teleport rebuilds physics. Only a submitted retry may wait
        // through that absence; fresh ownership is still required for arrival.
        return start_ && frame.client==start_->client && frame.world==start_->owner.world &&
            (frame.entity==start_->owner.entity || (returning() && !frame.owned && !frame.entity)) &&
            frame.map==start_->map && frame.generation==start_->generation;
    }
    bool at_start(const StartTransform& pose) const {
        if (!valid_start_transform(pose)) return false;
        const float x=pose[12]-start_->transform[12],z=pose[14]-start_->transform[14];
        // Native ground checks can adjust height slightly. Require horizontal
        // placement within 0.75m and every orientation axis within ~14 degrees.
        if (x*x+z*z>.75f*.75f || std::abs(pose[13]-start_->transform[13])>1.5f) return false;
        for (unsigned row=0;row<3;++row) {
            float dot{}; for (unsigned axis=0;axis<3;++axis) dot+=pose[row*4+axis]*start_->transform[row*4+axis];
            if (dot<.97f) return false;
        }
        return true;
    }
    std::optional<SavedStart> start_;
    std::optional<LocalBailOwner> matched_owner_;
    SkaterTeleportReceipt receipt_;
    Phase phase_=Phase::idle;
    std::uint64_t until_{},sent_at_{},matched_at_{};
    std::string status_="Start an attempt to save its location and orientation.";
};
}
