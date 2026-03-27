#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Dto/Account/BalanceSnapshot.hpp"
#include "Dto/Market/Binance/TradeKline.hpp"
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/Account/AccountPolicies.hpp"
#include "Exchanges/BinanceSimulator/Contracts/AccountInitConfig.hpp"
#include "Exchanges/BinanceSimulator/Contracts/OrderRejectInfo.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

/// Reduced account state owner used by the simulator compatibility layer.
/// Policy-driven order fill logic is delegated to Domain services.
class Account {
public:
    using AccountInitConfig = Contracts::AccountInitConfig;

    /// Enumerates self-trade-prevention options preserved on API signatures.
    enum class SelfTradePreventionMode {
        None = 0,
        ExpireTaker = 1,
        ExpireMaker = 2,
        ExpireBoth = 3,
    };

    Account() = default;
    /// Legacy compatibility constructor mapping init balance into perp wallet.
    Account(double init_balance, int);
    /// Compatibility constructor with policy-injection hooks for reduced matching path.
    Account(double init_balance, int vip_level, const AccountPolicies& policies);
    /// Initializes spot/perp balance ledgers from bootstrap config.
    explicit Account(const AccountInitConfig& init);
    /// Initializes spot/perp balance ledgers with policy-injection hooks.
    Account(const AccountInitConfig& init, const AccountPolicies& policies);

    /// Returns immutable spot balance snapshot.
    QTrading::Dto::Account::BalanceSnapshot get_spot_balance() const { return spot_balance_; }
    /// Returns immutable perp balance snapshot.
    QTrading::Dto::Account::BalanceSnapshot get_perp_balance() const { return perp_balance_; }
    /// Legacy compatibility accessor for perp balance snapshot.
    QTrading::Dto::Account::BalanceSnapshot get_balance() const;
    /// Returns aggregate cash across ledgers.
    double get_total_cash_balance() const { return total_cash_balance_; }
    /// Returns mutation version for account state snapshots.
    uint64_t get_state_version() const;
    /// Returns perp wallet cash balance.
    double get_wallet_balance() const;
    /// Returns spot cash balance.
    double get_spot_cash_balance() const;
    /// Returns equity in current reduced account model.
    double get_equity() const;
    /// Returns unrealized pnl in current reduced account model.
    double total_unrealized_pnl() const;
    /// Applies spot cash delta and keeps snapshot fields aligned.
    void apply_spot_cash_delta(double delta);
    /// Applies perp wallet delta and keeps snapshot fields aligned.
    void apply_perp_wallet_delta(double delta);
    /// Available-balance check for spot-side cash operations.
    bool can_debit_spot(double amount) const
    {
        return amount <= 0.0 || spot_balance_.AvailableBalance + 1e-12 >= amount;
    }
    /// Available-balance check for perp-side cash operations.
    bool can_debit_perp(double amount) const
    {
        return amount <= 0.0 || perp_balance_.AvailableBalance + 1e-12 >= amount;
    }
    /// Moves cash from spot to perp if available balance is sufficient.
    bool transfer_spot_to_perp(double amount);
    /// Moves cash from perp to spot if available balance is sufficient.
    bool transfer_perp_to_spot(double amount);
    /// Stores per-symbol leverage for reduced perp opening-path bookkeeping.
    void set_symbol_leverage(const std::string& symbol, double new_leverage);
    /// Submits reduced-scope order entries used by policy-injection tests.
    bool place_order(const std::string& symbol,
        double quantity,
        double price,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        bool reduce_only = false,
        const std::string& client_order_id = {},
        SelfTradePreventionMode stp_mode = SelfTradePreventionMode::None);
    /// Applies reduced-scope order matching against a per-symbol kline snapshot.
    void update_positions(const std::unordered_map<std::string, QTrading::Dto::Market::Binance::TradeKlineDto>& symbol_kline);
    /// Applies reduced-scope order matching with optional mark context for policy hooks.
    void update_positions(const std::unordered_map<std::string, QTrading::Dto::Market::Binance::TradeKlineDto>& symbol_kline,
        const std::unordered_map<std::string, double>& symbol_mark_price);
    /// Returns reduced-scope position list.
    const std::vector<QTrading::dto::Position>& get_all_positions() const { return positions_; }

private:
    static AccountInitConfig build_init_from_balance_(double init_balance);
    static double validate_non_negative_(double value, const char* field);
    static void validate_non_negative_int_(int value, const char* field);
    static QTrading::Dto::Account::BalanceSnapshot make_balance_(double wallet);
    static void sync_snapshot_(QTrading::Dto::Account::BalanceSnapshot& snapshot);
    void sync_total_cash_();

    QTrading::Dto::Account::BalanceSnapshot spot_balance_{ make_balance_(1'000'000.0) };
    QTrading::Dto::Account::BalanceSnapshot perp_balance_{};
    double total_cash_balance_{ 1'000'000.0 };
    int vip_level_{ 0 };
    AccountPolicies policies_{};
    std::unordered_map<std::string, double> symbol_leverage_{};
    std::vector<QTrading::dto::Order> open_orders_{};
    std::vector<QTrading::dto::Position> positions_{};
    int next_order_id_{ 1 };
    uint64_t state_version_{ 0 };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim

using Account = QTrading::Infra::Exchanges::BinanceSim::Account;
