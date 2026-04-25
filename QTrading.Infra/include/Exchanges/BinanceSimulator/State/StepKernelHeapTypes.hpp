#pragma once

#include <cstddef>
#include <cstdint>

namespace QTrading::Infra::Exchanges::BinanceSim::State {

/// Timestamp-ordered heap entry used for per-symbol replay and funding queues.
struct StepKernelHeapItem {
    /// Next timestamp scheduled for the symbol.
    uint64_t ts{ 0 };
    /// Symbol id associated with the timestamp.
    size_t sym_id{ 0 };
};

/// Min-heap comparator for `StepKernelHeapItem`.
struct StepKernelHeapItemGreater {
    /// Orders earlier timestamps first and breaks ties by symbol id.
    bool operator()(const StepKernelHeapItem& a, const StepKernelHeapItem& b) const noexcept
    {
        if (a.ts != b.ts) {
            return a.ts > b.ts;
        }
        return a.sym_id > b.sym_id;
    }
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
