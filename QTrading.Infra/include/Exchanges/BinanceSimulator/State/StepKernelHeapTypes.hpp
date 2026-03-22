#pragma once

#include <cstddef>
#include <cstdint>

namespace QTrading::Infra::Exchanges::BinanceSim::State {

struct StepKernelHeapItem {
    uint64_t ts{ 0 };
    size_t sym_id{ 0 };
};

struct StepKernelHeapItemGreater {
    bool operator()(const StepKernelHeapItem& a, const StepKernelHeapItem& b) const noexcept
    {
        if (a.ts != b.ts) {
            return a.ts > b.ts;
        }
        return a.sym_id > b.sym_id;
    }
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
