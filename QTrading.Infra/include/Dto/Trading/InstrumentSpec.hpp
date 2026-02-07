#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace QTrading::Dto::Trading {

enum class InstrumentType : std::uint8_t {
    Spot = 0,
    Perp = 1,
};

struct InstrumentSpec {
    InstrumentType type{ InstrumentType::Perp };
    double max_leverage{ 125.0 };
    bool allow_short{ true };
    bool funding_enabled{ true };
    bool maintenance_margin_enabled{ true };
};

inline InstrumentSpec SpotInstrumentSpec()
{
    return InstrumentSpec{
        InstrumentType::Spot,
        1.0,
        false,
        false,
        false
    };
}

inline InstrumentSpec PerpInstrumentSpec()
{
    return InstrumentSpec{
        InstrumentType::Perp,
        125.0,
        true,
        true,
        true
    };
}

class InstrumentRegistry {
public:
    void Set(const std::string& symbol, const InstrumentSpec& spec)
    {
        specs_[symbol] = spec;
        implicit_cache_.erase(symbol);
    }

    void Set(const std::string& symbol, InstrumentType type)
    {
        Set(symbol, type == InstrumentType::Spot ? SpotInstrumentSpec() : PerpInstrumentSpec());
    }

    bool HasExplicit(const std::string& symbol) const
    {
        return specs_.find(symbol) != specs_.end();
    }

    const InstrumentSpec& Resolve(const std::string& symbol) const
    {
        const auto it = specs_.find(symbol);
        if (it != specs_.end()) {
            return it->second;
        }

        const auto fit = implicit_cache_.find(symbol);
        if (fit != implicit_cache_.end()) {
            return fit->second;
        }

        // No symbol-name inference: unspecified symbols default to perp policy.
        auto [ins, ok] = implicit_cache_.emplace(symbol, PerpInstrumentSpec());
        (void)ok;
        return ins->second;
    }

private:
    std::unordered_map<std::string, InstrumentSpec> specs_{};
    mutable std::unordered_map<std::string, InstrumentSpec> implicit_cache_{};
};

} // namespace QTrading::Dto::Trading
