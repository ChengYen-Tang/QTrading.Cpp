#include "Exchanges/BinanceSimulator/Output/LegacyEventEnvelopeEmitter.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

LegacyEventEnvelopeEmitter::LegacyEventEnvelopeEmitter(BinanceExchange& exchange) noexcept
    : exchange_(exchange)
{
}

void LegacyEventEnvelopeEmitter::emit(BinanceExchange::EventEnvelope&& envelope) const
{
    exchange_.log_status_snapshot(
        envelope.account_snapshot.perp_balance,
        envelope.account_snapshot.spot_balance,
        envelope.account_snapshot.total_cash_balance,
        envelope.account_snapshot.spot_inventory_value,
        envelope.position_snapshot.positions,
        envelope.order_snapshot.orders,
        envelope.cur_ver);

    exchange_.log_events(envelope.ctx,
        envelope.market_events,
        envelope.position_snapshot.positions,
        envelope.order_snapshot.orders,
        envelope.funding_events,
        envelope.account_snapshot.perp_balance,
        envelope.account_snapshot.spot_balance,
        envelope.account_snapshot.total_cash_balance,
        envelope.account_snapshot.spot_inventory_value,
        std::move(envelope.fill_events),
        envelope.cur_ver);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output

