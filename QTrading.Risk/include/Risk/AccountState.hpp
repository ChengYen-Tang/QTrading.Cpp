#pragma once

#include <optional>
#include <vector>
#include "Dto/Account/BalanceSnapshot.hpp"
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"

namespace QTrading::Risk {

/// @brief Snapshot of account state used by risk and monitoring.
struct AccountState {
    /// @brief Current open orders.
    std::vector<QTrading::dto::Order> open_orders;
    /// @brief Current positions.
    std::vector<QTrading::dto::Position> positions;
    /// @brief Optional spot-ledger snapshot for dual-ledger aware risk logic.
    std::optional<QTrading::Dto::Account::BalanceSnapshot> spot_balance;
    /// @brief Optional perp-ledger snapshot for dual-ledger aware risk logic.
    std::optional<QTrading::Dto::Account::BalanceSnapshot> perp_balance;
    /// @brief Optional total cash (spot cash + perp wallet cash).
    std::optional<double> total_cash_balance;
};

} // namespace QTrading::Risk
