#pragma once
#include <cstdint>
#include <optional>

namespace dingosdk {
// On board the selector returns 300. Offboard retains 504 and sends the same
// request to animation; it must never receive an invented physics transition.
inline bool manual_bail_state(std::uint32_t state) noexcept {
    return (state>=100 && state<=104) || (state>=200 && state<=203) ||
        (state>=400 && state<=406) || (state>=500 && state<=503) ||
        (state>=600 && state<=605) || state==504;
}
// Publication is not consumption: animation can run less often than physics.
// Keep delivering for at most 500ms, until native ragdoll entry confirms it.
// This stores SDK data only. Native request bits remain scoped to each call.
template<class Owner> class ManualBailRequest {
public:
    struct Ticket { Owner owner; std::uint64_t serial{}; };
    std::optional<Ticket> ticket() const noexcept {
        return phase_==Phase::idle ? std::nullopt : std::optional(Ticket{owner_,serial_});
    }
    bool pending(const Ticket& ticket,std::uint64_t now) const noexcept {
        return phase_!=Phase::idle && matches(ticket) && now<until_;
    }
    bool queue(const Owner& owner,std::uint64_t now) noexcept {
        if (now>=until_) cancel();
        if (phase_!=Phase::idle) return false;
        owner_=owner; until_=now+500; ++serial_; phase_=Phase::queued; return true;
    }
    std::optional<Ticket> begin_selection(const Owner& owner,std::uint64_t now,
        std::uint32_t state,bool allowed) noexcept {
        if (owner!=owner_) return {};
        if (phase_==Phase::selected || phase_==Phase::awaiting) return response(owner,now,allowed);
        if (phase_!=Phase::queued) return {};
        if (!allowed || now>=until_ || !manual_bail_state(state)) {cancel(); return {};}
        selection_state_=state; phase_=Phase::selecting; return Ticket{owner_,serial_};
    }
    bool selected(const Ticket& ticket,std::uint32_t next,bool installed) noexcept {
        if (!matches(ticket) || phase_!=Phase::selecting) return false;
        if (!installed || (next!=300 && !(selection_state_==504 && next==504))) {cancel(); return false;}
        phase_=Phase::selected; return true;
    }
    std::optional<Ticket> response(const Owner& owner,std::uint64_t now,bool allowed) noexcept {
        if (owner!=owner_ || (phase_!=Phase::selected && phase_!=Phase::awaiting)) return {};
        if (!allowed || now>=until_) {cancel(); return {};}
        return Ticket{owner_,serial_};
    }
    std::optional<Ticket> begin_publication(const Owner& owner,std::uint64_t now,bool allowed) noexcept {
        if (owner!=owner_ || (phase_!=Phase::selected && phase_!=Phase::awaiting)) return {};
        if (!allowed || now>=until_) {cancel(); return {};}
        phase_=Phase::publishing; return Ticket{owner_,serial_};
    }
    bool published(const Ticket& ticket) noexcept {
        if (!matches(ticket) || phase_!=Phase::publishing) return false;
        phase_=Phase::awaiting;
        const bool first=!published_; published_=true; return first;
    }
    void cancel() noexcept { phase_=Phase::idle; published_=false; }
    void cancel(const Ticket& ticket) noexcept { if (matches(ticket)) cancel(); }
private:
    enum class Phase {idle,queued,selecting,selected,publishing,awaiting};
    bool matches(const Ticket& ticket) const noexcept {return ticket.serial==serial_ && ticket.owner==owner_;}
    Owner owner_{};
    std::uint64_t until_{},serial_{};
    std::uint32_t selection_state_{};
    bool published_{};
    Phase phase_=Phase::idle;
};
}
