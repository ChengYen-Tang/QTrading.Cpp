#include "Exchanges/BinanceSimulator/Output/EventEnvelopePublisher.hpp"
#include "Exchanges/BinanceSimulator/Output/EventPublisherPipeline.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

EventEnvelopePublisher::EventEnvelopePublisher(BinanceExchange& exchange) noexcept
    : exchange_(exchange)
{
}

void EventEnvelopePublisher::publish(BinanceExchange::EventEnvelope&& task) const
{
    const BinanceExchange::EventPublishMode mode =
        exchange_.event_publish_mode_.load(std::memory_order_acquire);
    if (mode == BinanceExchange::EventPublishMode::DualPublishCompare) {
        BinanceExchange::EventPublishCompareDiagnostic diag{};
        diag.mode = mode;
        diag.compared = true;
        diag.legacy = exchange_.build_event_publish_compare_snapshot_(task);
        diag.candidate = exchange_.build_event_publish_compare_snapshot_(task);
        const auto mismatch = exchange_.compare_event_publish_snapshots_(diag.legacy, diag.candidate);
        diag.matched = !mismatch.has_value();
        if (mismatch.has_value()) {
            diag.reason = *mismatch;
        }
        exchange_.record_event_publish_diagnostic_(std::move(diag));
    }

    if (exchange_.event_publisher_) {
        exchange_.event_publisher_->publish(std::move(task));
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
