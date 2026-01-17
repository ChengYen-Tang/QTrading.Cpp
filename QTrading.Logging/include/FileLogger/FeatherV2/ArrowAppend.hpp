#pragma once

#include <arrow/api.h>
#include <stdexcept>

namespace QTrading::Log::FileLogger::FeatherV2::detail {

    template <typename Builder, typename Value>
    inline void AppendOrThrow(Builder* builder, const Value& value)
    {
        const auto st = builder->Append(value);
        if (!st.ok()) {
            throw std::runtime_error(st.ToString());
        }
    }

} // namespace QTrading::Log::FileLogger::FeatherV2::detail
