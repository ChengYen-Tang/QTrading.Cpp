#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

struct OrderRejectInfo {
    enum class Code {
        None = 0,
        Unknown = 1,
        NotImplemented = 2,
    };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
