#pragma once

namespace QTrading::Log {
    enum class LogModule {
        Orders,
        Positions,
        Account
    };

    const std::string& LogModuleToString(LogModule module) {
        switch (module) {
        case LogModule::Orders:   return "Orders";
        case LogModule::Positions:return "Positions";
        case LogModule::Account:  return "Account";
        default:                  return "Unknown";
        }
    }
}
