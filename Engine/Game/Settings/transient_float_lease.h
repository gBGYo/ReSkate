#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace dingosdk {
struct FloatSettingSample {
    std::uintptr_t manager{},address{},type{};
    float value{};
    bool operator==(const FloatSettingSample&) const = default;
    bool same_field(const FloatSettingSample& other) const noexcept {
        return manager==other.manager && address==other.address && type==other.type;
    }
};
// A temporary multiplier over the current value. The provider must validate
// the native type and thread, and recheck `expected` before its typed setter.
class TransientFloatLease {
public:
    bool active() const noexcept {return original_.has_value();}
    bool owns(const FloatSettingSample& sample) const noexcept {
        return (applied_ && sample==*applied_) || (writing_ && sample==*writing_);
    }
    template<class Read,class Write> bool begin(float factor,Read&& read,Write&& write) {
        if (active() || !valid_factor(factor)) return false;
        const auto current=read();
        if (!current || !std::isfinite(current->value) || current->value<.01f || current->value>10) return false;
        original_=applied_=current;
        if (update(factor,read,write)) return true;
        (void)restore(read,write);
        return false;
    }
    template<class Read,class Write> bool update(float factor,Read&& read,Write&& write) {
        if (!active() || !valid_factor(factor)) return false;
        const auto current=read();
        if (!current) return false;
        if (!applied_ || *current!=*applied_) {clear(); return false;}
        return apply(original_->value*factor,*current,read,write);
    }
    template<class Read,class Write> bool restore(Read&& read,Write&& write) {
        if (!active()) return true;
        const auto current=read();
        if (!current) return false; // Temporarily unavailable: retain for retry.
        if (!applied_ || *current!=*applied_) {clear(); return true;}
        if (!apply(original_->value,*current,read,write)) return false;
        clear(); return true;
    }
private:
    static bool valid_factor(float factor) noexcept {return std::isfinite(factor) && factor>=.1f && factor<=1;}
    void clear() noexcept {original_.reset(); applied_.reset(); writing_.reset();}
    template<class Read,class Write> bool apply(float value,const FloatSettingSample& current,Read&& read,Write&& write) {
        if (value==current.value) return true;
        writing_=current;
        applied_=current; applied_->value=value;
        const bool accepted=write(current,value);
        writing_.reset();
        const auto after=read();
        if (!after) return false;
        if (!accepted && *after==current) applied_=current;
        if (!owns(*after)) {clear(); return false;}
        return accepted;
    }
    std::optional<FloatSettingSample> original_,applied_,writing_;
};
}
