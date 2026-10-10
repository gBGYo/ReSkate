#include "blood_options.h"
#include "Engine/Core/Json/json.h"
namespace dingosdk::blood {
bool valid_options(const Options& v) noexcept {
    return std::isfinite(v.blood_strength) && v.blood_strength>=0 && v.blood_strength<=1 &&
        std::isfinite(v.blood_min_damage) && v.blood_min_damage>=0 && v.blood_min_damage<=3600 && valid_blood_tuning(v.blood_tuning);
}
std::string encode_options(const Options& v) {
    return Json{{"version",1},{"blood",v.blood},{"strength",v.blood_strength},{"minimumDamage",v.blood_min_damage},
        {"density",v.blood_tuning.density},{"width",v.blood_tuning.width},{"length",v.blood_tuning.length},
        {"bleeding",v.blood_tuning.bleeding},{"lifetime",v.blood_tuning.lifetime},{"bloodColor",unsigned(v.blood_tuning.color)}}.dump();
}
std::optional<Options> decode_options(std::string_view text) noexcept {
    try {
        const auto j=Json::parse(text);
        if (!j.is_object() || j.value("version",0)!=1) return {};
        Options v;
        v.blood=j.value("blood",v.blood); v.blood_strength=j.value("strength",v.blood_strength);
        v.blood_min_damage=j.value("minimumDamage",v.blood_min_damage);
        auto& t=v.blood_tuning;
        t.density=j.value("density",t.density); t.width=j.value("width",t.width); t.length=j.value("length",t.length);
        t.bleeding=j.value("bleeding",t.bleeding); t.lifetime=j.value("lifetime",t.lifetime);
        t.color=static_cast<BloodColor>(j.value("bloodColor",0u));
        if (valid_options(v)) return v;
    } catch (...) {}
    return {};
}
}
