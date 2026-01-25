#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include "ISignalEngine.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Signal {

/// @brief Basis z-score signal using spot and perp close prices.
class BasisSignalEngine final : public ISignalEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    /// @brief Configuration for basis signal.
    struct Config {
        std::string spot_symbol;
        std::string perp_symbol;
        std::size_t window = 120;
        double enter_z = 2.0;
        double exit_z = 0.5;
    };

    explicit BasisSignalEngine(Config cfg);

    /// @brief Update signal based on latest market snapshot.
    SignalDecision on_market(
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
    std::deque<double> spread_window_;
    bool has_symbol_ids_{ false };
    std::size_t spot_id_{ 0 };
    std::size_t perp_id_{ 0 };

    static double mean(const std::deque<double>& values);
    static double stddev(const std::deque<double>& values, double m);
};

} // namespace QTrading::Signal
