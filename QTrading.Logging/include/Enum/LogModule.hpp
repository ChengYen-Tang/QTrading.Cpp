#pragma once

namespace QTrading::Log {
    /// @enum LogModule
    /// @brief Identifies the different logging modules.
    enum class LogModule {
        Order,    ///< Order module
        Position, ///< Position module
        Account   ///< Account module
    };

    /// @brief Convert a LogModule enum to its string representation.
    /// @param module The LogModule value to convert.
    /// @return Reference to the corresponding module name string.
    inline const std::string& LogModuleToString(LogModule module) {
        static const std::string order = "Order";
        static const std::string position = "Position";
        static const std::string account = "Account";
        static const std::string unknown = "Unknown";

        switch (module) {
        case LogModule::Order:    return order;
        case LogModule::Position: return position;
        case LogModule::Account:  return account;
        default:                  return unknown;
        }
    }
}
