#pragma once

#include "SignalDecision.hpp"

namespace QTrading::Signal {

/// @brief Interface for turning market data into signal decisions.
template <typename TMarket>
class ISignalEngine {
public:
    /// @brief Virtual destructor.
    virtual ~ISignalEngine() = default;
    /// @brief Evaluate signal for a market snapshot.
    virtual SignalDecision on_market(const TMarket& market) = 0;
};

/// @brief No-op signal engine returning default decisions.
template <typename TMarket>
class NullSignalEngine final : public ISignalEngine<TMarket> {
public:
    SignalDecision on_market(const TMarket&) override { return SignalDecision{}; }
};

} // namespace QTrading::Signal
