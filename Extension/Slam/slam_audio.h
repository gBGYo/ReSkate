#pragma once
#include "slam_visuals.h"
#include <string>

namespace dingosdk::slam {
// Client thread only. Stops on conflicts/focus loss and consumes each new
// confirmed impact once. Engine preparation never blocks a physics callback.
// Explicit previews may play while the menu is open, but never out of focus.
std::string update_impact_audio(const VisualOptions& options,const VisualEvents& events,bool active,bool prepare_allowed,
    bool preview_requested=false) noexcept;
}
