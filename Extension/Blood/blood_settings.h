#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace dingosdk::blood {
enum class BloodColor : unsigned { red, green, blue, pink };
inline constexpr std::array blood_color_names={"Red","Green","Blue","Pink"};
inline constexpr std::array blood_color_suffixes={"","_green","_blue","_pink"};
inline constexpr bool valid_blood_color(BloodColor color) noexcept {return unsigned(color)<blood_color_names.size();}
inline std::string blood_colored_asset(std::string_view name,BloodColor color) {
    std::string result(name);
    if (valid_blood_color(color)) result.insert(result.ends_with("_dv") ? result.size()-3 : result.size(),blood_color_suffixes[unsigned(color)]);
    return result;
}
// Preserve the original red pigment exactly; other presets reuse its shading.
inline std::array<std::uint8_t,3> blood_pixel(BloodColor color,std::uint8_t red,std::uint8_t green,std::uint8_t blue) noexcept {
    switch (color) {
    case BloodColor::green: return {green,red,blue};
    case BloodColor::blue: return {green,blue,red};
    case BloodColor::pink: return {red,std::uint8_t(red/4),std::uint8_t(red*3/4)};
    default: return {red,green,blue};
    }
}
struct BloodTuning {
    float density=1, width=1, length=1, bleeding=1, lifetime=90;
    BloodColor color=BloodColor::red;
    bool operator==(const BloodTuning&) const = default;
};
inline bool valid_blood_tuning(const BloodTuning& v) noexcept {
    const auto range=[](float value,float lo,float hi){return std::isfinite(value) && value>=lo && value<=hi;};
    return range(v.density,.25f,3.f) && range(v.width,.25f,3.f) && range(v.length,.25f,3.f) &&
        range(v.bleeding,.25f,5.f) && range(v.lifetime,10.f,300.f) && valid_blood_color(v.color);
}
}
