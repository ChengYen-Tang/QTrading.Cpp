#pragma once

#include "Exchanges/BinanceSimulator/Account/Account.hpp"

/// \brief Default policy factory for `Account`.
/// \details Kept in a separate header so that `Account.cpp` stays focused on bookkeeping.
struct AccountPolicies {
    static Account::Policies Default();
};
