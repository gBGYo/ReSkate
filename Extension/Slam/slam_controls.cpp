#include "slam_controls.h"
#include "Engine/Core/Json/json.h"
#include <stdexcept>

namespace dingosdk::slam {
bool valid_bail_key(unsigned key) noexcept {
    if (!key) return true;
    if (key>0xfe) return false;
    // Windows mouse buttons, modifiers, system/menu keys and text controls.
    switch (key) {
    case 1: case 2: case 3: case 4: case 5: case 6:
    case 8: case 9: case 13: case 16: case 17: case 18: case 20: case 27: case 32:
    case 0x5b: case 0x5c: case 0x5d: case 0x90: case 0xa0: case 0xa1:
    case 0xa2: case 0xa3: case 0xa4: case 0xa5: case 0xe5: case 0xe7: return false;
    default: return true;
    }
}
bool valid_bail_controls(const BailControls& value) noexcept {
    return valid_bail_key(value.key) && valid_controller_combo(value.controller_combo);
}
std::string encode_bail_controls(const BailControls& value) {
    if (!valid_bail_controls(value)) throw std::invalid_argument("Invalid Slam bail bindings");
    return Json{{"version",1},{"enabled",value.enabled},{"key",value.key},{"controller",value.controller_combo}}.dump();
}
std::optional<BailControls> decode_bail_controls(std::string_view document) noexcept {
    try {
        const auto doc=Json::parse(document,{256,4,24});
        if (!doc.is_object() || !doc.contains("version") || !doc.at("version").is_number_integer() ||
            doc.at("version").get<std::int64_t>()!=1 || !doc.contains("enabled") || !doc.at("enabled").is_boolean() ||
            !doc.contains("key") || !doc.at("key").is_number_integer() ||
            !doc.contains("controller") || !doc.at("controller").is_number_integer()) return {};
        const auto key=doc.at("key").get<std::int64_t>(),combo=doc.at("controller").get<std::int64_t>();
        if (key<0 || key>0xfe || combo<0 || combo>controller_button_mask) return {};
        BailControls value{doc.at("enabled").get<bool>(),static_cast<unsigned>(key),static_cast<std::uint32_t>(combo)};
        return valid_bail_controls(value) ? std::optional(value) : std::nullopt;
    } catch (...) {return {};}
}
}
