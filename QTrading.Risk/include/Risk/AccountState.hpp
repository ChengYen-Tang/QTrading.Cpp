#pragma once

#include <vector>
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"

namespace QTrading::Risk {

/// @brief Snapshot of account state used by risk and monitoring.
struct AccountState {
    /// @brief Current open orders.
    std::vector<QTrading::dto::Order> open_orders;
    /// @brief Current positions.
    std::vector<QTrading::dto::Position> positions;
};

} // namespace QTrading::Risk
