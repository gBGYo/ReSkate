#pragma once
#include <cstdint>

namespace dingosdk::slam {
// Serialized by the runtime mutex. Render export is the final stage for a
// scene submission; an early evaluation for the next frame must not replace
// that pending pose before Present consumes it. Tickets also prevent a slow
// capture worker from publishing after a later callback or an owner change.
struct PosePublication {
    std::uint64_t issued{}, accepted{}, last_export{};
    bool pending_export{};
    std::uint64_t issue() noexcept { return ++issued; }
    bool publish(std::uint64_t ticket,bool render_export) noexcept {
        if (!ticket || ticket<=accepted || ticket>issued || (!render_export && pending_export)) return false;
        accepted=ticket;
        if (render_export) {last_export=ticket; pending_export=true;}
        return true;
    }
    void presented() noexcept { pending_export=false; }
    void invalidate() noexcept { accepted=++issued; last_export=0; pending_export=false; }
};
}
