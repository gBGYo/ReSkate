#pragma once
#include "blood_settings.h"
#include <optional>
namespace dingosdk::blood {
struct Options {
    bool blood{};
    float blood_strength=.7f, blood_min_damage=25;
    BloodTuning blood_tuning;
    bool operator==(const Options&) const = default;
};
bool valid_options(const Options&) noexcept;
std::string encode_options(const Options&);
std::optional<Options> decode_options(std::string_view) noexcept;
}
