#pragma once
#include "slam_model.h"
#include <optional>
#include <functional>
#include <map>
#include <string_view>

namespace dingosdk::slam {
// Versioned, bounded JSON strings fit the profile's scalar user-value store.
std::string encode_config(const Config& config);
std::optional<Config> decode_config(std::string_view document) noexcept;
std::string personal_best_key(std::string_view map, const Config& config);
struct PersonalBest {
    std::uint64_t score{};
    float progress{};
    unsigned attempts{}, successes{};
    // Only completed, non-cancelled results qualify. Called once per attempt.
    bool record(const Result& result) noexcept;
    bool operator==(const PersonalBest&) const = default;
};
std::string encode_personal_best(std::string_view map, const Config& config, const PersonalBest& best);
std::optional<PersonalBest> decode_personal_best(std::string_view document, std::string_view map, const Config& config) noexcept;
struct BestView {
    PersonalBest best;
    bool saved = true;
    bool improved{};
};
// Invoked on the client thread. Native callbacks queue completed results;
// they never access the profile. Serial numbers prevent duplicate counting,
// and failed writes retain the record for a later flush without recounting.
class BestBook {
public:
    using Read = std::function<std::optional<std::string>(std::string_view)>;
    using Write = std::function<bool(std::string_view,std::string_view)>;
    BestView lookup(std::string_view map,const Config& config,const Read& read) const;
    std::optional<BestView> record(std::uint64_t serial,std::string_view map,const Result& result,
        const Read& read,const Write& write);
    bool flush(const Write& write);
private:
    struct Entry { std::string map; Config config; PersonalBest best; bool dirty{}; };
    std::map<std::string,Entry,std::less<>> entries_;
    std::uint64_t last_serial_{};
};
}
