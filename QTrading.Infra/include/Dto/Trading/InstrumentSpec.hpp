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
    // Futures symbol-level liquidation fee. Negative means "unset" and falls back to simulator defaults.
    double liquidation_fee_rate{ -1.0 };

    // PRICE_FILTER
    double min_price{ 0.0 };
    double max_price{ 0.0 };
    double price_tick_size{ 0.0 };

    // LOT_SIZE
    double min_qty{ 0.0 };
    double max_qty{ 0.0 };
    double qty_step_size{ 0.0 };

    // MARKET_LOT_SIZE
    double market_min_qty{ 0.0 };
    double market_max_qty{ 0.0 };
    double market_qty_step_size{ 0.0 };

    // MIN_NOTIONAL / NOTIONAL
    double min_notional{ 0.0 };
    double max_notional{ 0.0 };

    // PERCENT_PRICE / PERCENT_PRICE_BY_SIDE
    // When percent_price_by_side is false, use percent_price_multiplier_{up,down}.
    // When true, use bid/ask multipliers by order side.
    bool percent_price_by_side{ false };
    double percent_price_multiplier_up{ 0.0 };
    double percent_price_multiplier_down{ 0.0 };
    double bid_multiplier_up{ 0.0 };
    double bid_multiplier_down{ 0.0 };
    double ask_multiplier_up{ 0.0 };
    double ask_multiplier_down{ 0.0 };

    // Futures symbol-level protection constraints.
    // trigger_protect is a max relative divergence between mark and index prices.
    // market_take_bound is a max relative divergence between market execution reference and mark.
    double trigger_protect{ 0.0 };
    double market_take_bound{ 0.0 };

    // Symbol-level STP policy.
    // default_stp_mode follows Account::SelfTradePreventionMode numeric values.
    // allowed_stp_modes_mask bit i enables mode i (0=None,1=ExpireTaker,2=ExpireMaker,3=ExpireBoth).
    int default_stp_mode{ 0 };
    uint8_t allowed_stp_modes_mask{ 0x0F };
};

inline InstrumentSpec SpotInstrumentSpec()
{
    InstrumentSpec spec;
    spec.type = InstrumentType::Spot;
    spec.max_leverage = 1.0;
    spec.allow_short = false;
    spec.funding_enabled = false;
    spec.maintenance_margin_enabled = false;
    return spec;
}

inline InstrumentSpec PerpInstrumentSpec()
{
    InstrumentSpec spec;
    spec.type = InstrumentType::Perp;
    spec.max_leverage = 125.0;
    spec.allow_short = true;
    spec.funding_enabled = true;
    spec.maintenance_margin_enabled = true;
    return spec;
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
