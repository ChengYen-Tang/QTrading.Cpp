#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"

/// @brief AccountCoreV2 keeps legacy Account behavior while exposing a stable state/command facade.
///        It is intended for coexistence/compare flows and does not alter production default routing.
class AccountCoreV2 final {
public:
    enum class CommandKind {
        PlaceLimitOrder = 0,
        PlaceMarketOrder = 1,
        CancelOpenOrders = 2,
        ClosePosition = 3,
        ApplyFunding = 4,
    };

    struct Command {
        CommandKind kind{ CommandKind::PlaceLimitOrder };
        std::string symbol;
        double quantity{ 0.0 };
        double price{ 0.0 };
        QTrading::Dto::Trading::OrderSide side{ QTrading::Dto::Trading::OrderSide::Buy };
        QTrading::Dto::Trading::PositionSide position_side{ QTrading::Dto::Trading::PositionSide::Both };
        bool reduce_only{ false };
        std::string client_order_id;
        Account::SelfTradePreventionMode stp_mode{ Account::SelfTradePreventionMode::None };
        uint64_t funding_time{ 0 };
        double funding_rate{ 0.0 };
        double mark_price{ 0.0 };
    };

    struct CommandResult {
        bool accepted{ true };
        std::optional<Account::OrderRejectInfo> reject_info{};
        std::vector<Account::FundingApplyResult> funding_results{};
        Account::AccountStateView state_after{};
    };

    explicit AccountCoreV2(const Account::AccountInitConfig& init_config);
    AccountCoreV2(const Account::AccountInitConfig& init_config, Account::Policies policies);

    const Account::AccountStateView snapshot_state() const;
    CommandResult apply_command(const Command& command);

    Account& mutable_legacy_account() noexcept { return account_; }
    const Account& legacy_account() const noexcept { return account_; }

private:
    Account account_;
};

