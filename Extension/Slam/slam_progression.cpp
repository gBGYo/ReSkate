#include "slam_progression.h"
#include "Engine/Core/Json/json.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace dingosdk::slam {
namespace {
Json config_json(const Config& c) {
    if (!valid_config(c)) throw std::invalid_argument("Invalid Slam configuration");
    const auto& s = c.scoring;
    auto doc=Json{{"version",1}, {"kind",static_cast<unsigned>(c.kind)}, {"target",c.target},
        {"rules",Json::array({s.impact_rate,s.fracture_bonus,s.fall_rate,s.airtime_rate,s.slide_rate,
            s.chain_step,s.chain_window_s,s.head_fracture,s.limb_fracture})}};
    // Omitting the legacy default preserves existing personal-best keys.
    if (s.bruise_threshold>0) doc["bruiseThreshold"]=s.bruise_threshold;
    return doc;
}
bool bounded_integer(const Json& value, std::uint64_t maximum) {
    return value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>() >= 0) &&
        value.get<std::uint64_t>() <= maximum;
}
std::optional<Config> read_config(const Json& doc) {
    if (!doc.is_object() || !doc.contains("version") || !bounded_integer(doc.at("version"),1) || doc.at("version").get<unsigned>() != 1 ||
        !doc.contains("kind") || !bounded_integer(doc.at("kind"),static_cast<unsigned>(ChallengeKind::count)-1) ||
        !doc.contains("target") || !doc.at("target").is_number() || !doc.contains("rules")) return {};
    const auto& rules = doc.at("rules");
    if (!rules.is_array() || rules.size() != 9) return {};
    Config c;
    c.kind = static_cast<ChallengeKind>(doc.at("kind").get<unsigned>());
    const double target = doc.at("target").get<double>();
    if (!std::isfinite(target) || target < 1 || target > 1000000) return {};
    c.target = static_cast<float>(target);
    std::array<float*,9> fields{&c.scoring.impact_rate,&c.scoring.fracture_bonus,&c.scoring.fall_rate,
        &c.scoring.airtime_rate,&c.scoring.slide_rate,&c.scoring.chain_step,&c.scoring.chain_window_s,
        &c.scoring.head_fracture,&c.scoring.limb_fracture};
    for (std::size_t i=0; i<fields.size(); ++i) {
        if (!rules.at(i).is_number()) return {};
        const double value = rules.at(i).get<double>();
        if (!std::isfinite(value) || value < 0 || value > 10000) return {};
        *fields[i] = static_cast<float>(value);
    }
    if (doc.contains("bruiseThreshold")) {
        if (!doc.at("bruiseThreshold").is_number()) return {};
        const double value=doc.at("bruiseThreshold").get<double>();
        if (!std::isfinite(value) || value<0 || value>10000) return {};
        c.scoring.bruise_threshold=static_cast<float>(value);
    }
    return valid_config(c) ? std::optional(c) : std::nullopt;
}
bool valid_map(std::string_view map) { return !map.empty() && map.size() <= 512 && map.find('\0') == std::string_view::npos; }
}
std::string encode_config(const Config& config) { return config_json(config).dump(); }
std::optional<Config> decode_config(std::string_view document) noexcept {
    try { return read_config(Json::parse(document, {4096,8,128})); } catch (...) { return {}; }
}
std::string personal_best_key(std::string_view map, const Config& config) {
    if (!valid_map(map)) throw std::invalid_argument("Invalid Slam map");
    // Stable across processes/compiler versions. Full identity is also stored
    // and checked on read so a hash collision cannot mix two leaderboards.
    std::uint64_t hash = 14695981039346656037ull;
    const auto add = [&](std::string_view text) {
        for (const unsigned char c : text) { hash ^= c; hash *= 1099511628211ull; }
    };
    add(map); add("\n"); add(encode_config(config));
    return std::format("Slam.Best.v1.{:016x}", hash);
}
bool PersonalBest::record(const Result& result) noexcept {
    if (result.phase != Phase::results || result.cancelled || !valid_config(result.config) ||
        !std::isfinite(result.target_progress) || result.target_progress < 0) return false;
    const bool improved = result.points > score || result.target_progress > progress;
    score = std::max(score, result.points);
    progress = std::max(progress, result.target_progress);
    attempts = std::min(attempts,999999999u)+1;
    if (result.target_met) successes = std::min(successes,999999999u)+1;
    return improved;
}
std::string encode_personal_best(std::string_view map, const Config& config, const PersonalBest& best) {
    if (!valid_map(map)) throw std::invalid_argument("Invalid Slam map");
    return Json{{"version",1},{"map",map},{"config",config_json(config)},{"score",best.score},
        {"progress",best.progress},{"attempts",best.attempts},{"successes",best.successes}}.dump();
}
std::optional<PersonalBest> decode_personal_best(std::string_view document, std::string_view map, const Config& config) noexcept {
    try {
        const auto doc = Json::parse(document,{4096,8,256});
        if (!doc.is_object() || !doc.contains("version") || !bounded_integer(doc.at("version"),1) || doc.at("version").get<unsigned>() != 1 ||
            !doc.contains("map") || !doc.at("map").is_string() || doc.at("map").string() != map || !valid_map(map) ||
            !doc.contains("config") || read_config(doc.at("config")) != std::optional(config) ||
            !doc.contains("score") || !bounded_integer(doc.at("score"),1000000000000000ull) ||
            !doc.contains("attempts") || !bounded_integer(doc.at("attempts"),1000000000) ||
            !doc.contains("successes") || !bounded_integer(doc.at("successes"),1000000000) ||
            !doc.contains("progress") || !doc.at("progress").is_number()) return {};
        const double progress = doc.at("progress").get<double>();
        if (!std::isfinite(progress) || progress < 0 || progress > 1e15) return {};
        PersonalBest best{doc.at("score").get<std::uint64_t>(),static_cast<float>(progress),
            doc.at("attempts").get<unsigned>(),doc.at("successes").get<unsigned>()};
        return best.successes <= best.attempts ? std::optional(best) : std::nullopt;
    } catch (...) { return {}; }
}
namespace {
std::string best_identity(std::string_view map,const Config& config) {
    if (!valid_map(map)) throw std::invalid_argument("Invalid Slam map");
    return std::string(map)+"\n"+encode_config(config);
}
PersonalBest load_best(std::string_view map,const Config& config,const BestBook::Read& read) {
    const auto saved=read(personal_best_key(map,config));
    return saved ? decode_personal_best(*saved,map,config).value_or(PersonalBest{}) : PersonalBest{};
}
}
BestView BestBook::lookup(std::string_view map,const Config& config,const Read& read) const {
    const auto found=entries_.find(best_identity(map,config));
    if (found!=entries_.end()) return {found->second.best,!found->second.dirty,false};
    return {load_best(map,config,read),true,false};
}
std::optional<BestView> BestBook::record(std::uint64_t serial,std::string_view map,const Result& result,
    const Read& read,const Write& write) {
    if (!serial || serial<=last_serial_ || result.phase!=Phase::results || result.cancelled ||
        !valid_config(result.config) || !std::isfinite(result.target_progress) || result.target_progress<0) return {};
    const auto identity=best_identity(map,result.config);
    auto found=entries_.find(identity);
    if (found==entries_.end()) {
        const auto best=load_best(map,result.config,read);
        // Saved entries can be reloaded. Never discard an unsaved attempt.
        if (entries_.size()>=128) {
            const auto saved=std::find_if(entries_.begin(),entries_.end(),[](const auto& item) {return !item.second.dirty;});
            if (saved!=entries_.end()) entries_.erase(saved);
        }
        found=entries_.emplace(identity,Entry{std::string(map),result.config,best,false}).first;
    }
    auto& entry=found->second;
    const auto improved=entry.best.record(result);
    last_serial_=serial;
    entry.dirty=true;
    entry.dirty=!write(personal_best_key(entry.map,entry.config),encode_personal_best(entry.map,entry.config,entry.best));
    return BestView{entry.best,!entry.dirty,improved};
}
bool BestBook::flush(const Write& write) {
    bool saved=true;
    for (auto& [identity,entry] : entries_) {
        (void)identity;
        if (entry.dirty) entry.dirty=!write(personal_best_key(entry.map,entry.config),encode_personal_best(entry.map,entry.config,entry.best));
        saved=saved && !entry.dirty;
    }
    return saved;
}
}
