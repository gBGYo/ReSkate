#pragma once
#include "Engine/Game/Input/controller_bindings.h"
#include <optional>
#include <string>
#include <string_view>

namespace dingosdk::slam {
struct BailControls {
    bool enabled=true;
    unsigned key=0x77; // F8, zero unbinds it
    std::uint32_t controller_combo{};
    bool operator==(const BailControls&) const = default;
};
bool valid_bail_key(unsigned key) noexcept;
bool valid_bail_controls(const BailControls& controls) noexcept;
std::string encode_bail_controls(const BailControls& controls);
std::optional<BailControls> decode_bail_controls(std::string_view document) noexcept;
inline bool overlapping_combos(std::uint32_t first,std::uint32_t second) noexcept {
    return first && second && ((first&second)==first || (first&second)==second);
}
struct KeyboardPressLatch {
    unsigned key{};
    bool armed{};
    bool update(unsigned binding,bool down,bool inhibited) noexcept {
        if (key!=binding) {key=binding; armed=false;}
        if (inhibited || !key || !valid_bail_key(key)) {armed=false; return false;}
        if (!down) armed=true;
        if (!armed || !down) return false;
        armed=false; return true;
    }
};
}
