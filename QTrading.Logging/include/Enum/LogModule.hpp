#pragma once

namespace QTrading::Log {
    enum class LogModule {
        Order,
        Position,
        Account
    };

    inline const std::string& LogModuleToString(LogModule module) {
        static const std::string order = "Order";
        static const std::string position = "Position";
        static const std::string account = "Account";
        static const std::string unknown = "Unknown";

        switch (module) {
        case LogModule::Order:   return order;
        case LogModule::Position:return position;
        case LogModule::Account:  return account;
        default:                  return unknown;
        }
    }
}
