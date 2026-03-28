#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

struct AccountInitConfig {
    double init_balance{ 1'000'000.0 };
    double spot_initial_cash{ 1'000'000.0 };
    double perp_initial_wallet{ 0.0 };
    int vip_level{ 0 };
    bool hedge_mode{ false };
    bool strict_binance_mode{ true };
    bool merge_positions_enabled{ true };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
