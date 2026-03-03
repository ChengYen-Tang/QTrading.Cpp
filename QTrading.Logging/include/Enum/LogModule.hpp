#pragma once

namespace QTrading::Log {
    /// @enum LogModule
    /// @brief Identifies the different logging modules.
    enum class LogModule {
        Order,    ///< Order module
        Position, ///< Position module
        Account,  ///< Account module
        OrderEvent,
        PositionEvent,
        AccountEvent,
        MarketEvent,
        FundingEvent,
        RunMetadata
    };

    /// @brief Convert a LogModule enum to its string representation.
    /// @param module The LogModule value to convert.
    /// @return Reference to the corresponding module name string.
    inline const std::string& LogModuleToString(LogModule module) {
        static const std::string order = "Order";
        static const std::string position = "Position";
        static const std::string account = "Account";
        static const std::string order_event = "OrderEvent";
        static const std::string position_event = "PositionEvent";
        static const std::string account_event = "AccountEvent";
        static const std::string market_event = "MarketEvent";
        static const std::string funding_event = "FundingEvent";
        static const std::string run_metadata = "RunMetadata";
        static const std::string unknown = "Unknown";

        switch (module) {
        case LogModule::Order:         return order;
        case LogModule::Position:      return position;
        case LogModule::Account:       return account;
        case LogModule::OrderEvent:    return order_event;
        case LogModule::PositionEvent: return position_event;
        case LogModule::AccountEvent:  return account_event;
        case LogModule::MarketEvent:   return market_event;
        case LogModule::FundingEvent:  return funding_event;
        case LogModule::RunMetadata:   return run_metadata;
        default:                  return unknown;
        }
    }
}
