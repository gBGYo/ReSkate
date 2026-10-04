#pragma once
#include "Extension/Rendering/replay_frame.h"

namespace dingosdk::slam {struct Snapshot;}
namespace dingosdk::overlay {
enum class ReplayCaptureResult {inactive,included,unavailable};
// Composite only the replay X-ray into an encoder-owned copy, before the
// native codec consumes it. Menus and other presentation UI are excluded.
// scene was frozen when this video's native texture copy was queued. Encoding
// must not resample the current editor pose, camera, options or injury clock.
ReplayCaptureResult capture_slam_frame(std::span<std::uint8_t> pixels,const replay_export::BgraFrame& frame,
    const slam::Snapshot& scene) noexcept;
}
