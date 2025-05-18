#pragma once
#include <atomic>

namespace QTrading::Utils {

	/// \brief Global timestamp shared across all components.
	/// \details Stores the current global timestamp (in milliseconds since epoch)
	///          used by loggers and exchanges for synchronized event ordering.
	inline std::atomic<unsigned long long> GlobalTimestamp{ 0ULL };

} // namespace QTrading::Utils
