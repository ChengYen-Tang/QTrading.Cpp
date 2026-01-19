#pragma once

#include "TradeIntent.hpp"
#include "Signal/SignalDecision.hpp"

namespace QTrading::Intent {

/// @brief Interface for converting signals into trade intents.
template <typename TMarket>
class IIntentBuilder {
public:
    /// @brief Virtual destructor.
    virtual ~IIntentBuilder() = default;
    /// @brief Build a trade intent from signal and market snapshot.
    virtual TradeIntent build(const QTrading::Signal::SignalDecision& signal,
        const TMarket& market) = 0;
};

/// @brief No-op intent builder returning empty intents.
template <typename TMarket>
class NullIntentBuilder final : public IIntentBuilder<TMarket> {
public:
    TradeIntent build(const QTrading::Signal::SignalDecision&, const TMarket&) override {
        return TradeIntent{};
    }
};

} // namespace QTrading::Intent
