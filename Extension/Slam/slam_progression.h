#pragma once
#include "slam_model.h"
#include <optional>
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
}
