#pragma once

namespace QTrading::Strategy {

class IStrategyRuntime {
public:
    virtual ~IStrategyRuntime() = default;

    virtual void RunOneCycle() = 0;
};

} // namespace QTrading::Strategy
