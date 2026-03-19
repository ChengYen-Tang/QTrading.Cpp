#include "Exchanges/BinanceSimulator/Account/AccountCoreV2.hpp"

#include <utility>

AccountCoreV2::AccountCoreV2(const Account::AccountInitConfig& init_config)
    : account_(init_config)
{
}

AccountCoreV2::AccountCoreV2(const Account::AccountInitConfig& init_config, Account::Policies policies)
    : account_(init_config, std::move(policies))
{
}

const Account::AccountStateView AccountCoreV2::snapshot_state() const
{
    return account_.snapshot_state_view();
}

AccountCoreV2::CommandResult AccountCoreV2::apply_command(const Command& command)
{
    CommandResult result{};

    switch (command.kind) {
    case CommandKind::PlaceLimitOrder:
        result.accepted = account_.place_order(
            command.symbol,
            command.quantity,
            command.price,
            command.side,
            command.position_side,
            command.reduce_only,
            command.client_order_id,
            command.stp_mode);
        result.reject_info = account_.consume_last_order_reject_info();
        break;

    case CommandKind::PlaceMarketOrder:
        result.accepted = account_.place_order(
            command.symbol,
            command.quantity,
            command.side,
            command.position_side,
            command.reduce_only,
            command.client_order_id,
            command.stp_mode);
        result.reject_info = account_.consume_last_order_reject_info();
        break;

    case CommandKind::CancelOpenOrders:
        account_.cancel_open_orders(command.symbol);
        result.accepted = true;
        break;

    case CommandKind::ClosePosition:
        account_.close_position(command.symbol, command.position_side, command.price);
        result.accepted = true;
        break;

    case CommandKind::ApplyFunding:
        result.funding_results = account_.apply_funding(
            command.symbol,
            command.funding_time,
            command.funding_rate,
            command.mark_price);
        result.accepted = true;
        break;

    default:
        result.accepted = false;
        break;
    }

    result.state_after = account_.snapshot_state_view();
    return result;
}
