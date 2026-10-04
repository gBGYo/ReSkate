#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace dingosdk::replay_export {
struct BgraFrame {
    std::uint32_t width{},height{},pitch{};
    std::size_t bytes{};
};
inline std::optional<BgraFrame> bgra_frame(std::uint32_t width,std::uint32_t height,std::uintptr_t pitch) noexcept {
    if (!width || !height || width>8192 || height>8192 || pitch<width*4ULL || pitch>65536) return {};
    return BgraFrame{width,height,static_cast<std::uint32_t>(pitch),
        static_cast<std::size_t>(height-1)*pitch+width*4ULL};
}
// The transparent GPU target contains premultiplied BGRA. Preserve the
// encoder's alpha and row padding; blend only the visible RGB components.
inline bool composite_bgra(std::span<std::uint8_t> video,const BgraFrame& frame,
    std::span<const std::uint8_t> layer,std::size_t layer_pitch) noexcept {
    const auto checked=bgra_frame(frame.width,frame.height,frame.pitch);
    if (!checked || checked->bytes!=frame.bytes || video.size()<frame.bytes ||
        layer_pitch<frame.width*4ULL || layer_pitch>65536 ||
        layer.size()<static_cast<std::size_t>(frame.height-1)*layer_pitch+frame.width*4ULL) return false;
    for (std::size_t y=0;y<frame.height;++y) {
        auto* target=video.data()+y*frame.pitch;
        const auto* source=layer.data()+y*layer_pitch;
        for (std::size_t x=0;x<frame.width;++x,target+=4,source+=4) {
            const auto alpha=source[3];
            if (!alpha) continue;
            for (std::size_t c=0;c<3;++c)
                target[c]=static_cast<std::uint8_t>(std::min(255u,source[c]+(target[c]*(255u-alpha)+127u)/255u));
        }
    }
    return true;
}
}
