#pragma once
#include <atomic>

namespace QTrading::Utils {
    inline std::atomic<unsigned long long> GlobalTimestamp{ 0ULL };
}
