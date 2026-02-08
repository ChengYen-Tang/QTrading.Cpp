#pragma once

#include <map>
#include <limits>
#include <vector>

/// @brief Fee rates for maker and taker sides.
struct FeeRate {
    double maker_fee_rate;   ///< Rate applied to limit (maker) orders
    double taker_fee_rate;   ///< Rate applied to market (taker) orders
};

/// @brief Mapping from VIP level (0–9) to USD-M perpetual maker/taker fees.
const std::map<int, FeeRate> vip_fee_rates = {
    {0, {0.00020, 0.00050}},
    {1, {0.00016, 0.00040}},
    {2, {0.00014, 0.00035}},
    {3, {0.00012, 0.00032}},
    {4, {0.00010, 0.00030}},
    {5, {0.00008, 0.00027}},
    {6, {0.00006, 0.00025}},
    {7, {0.00004, 0.00022}},
    {8, {0.00002, 0.00020}},
    {9, {0.00000, 0.00017}}
};

/// @brief Mapping from VIP level (0–9) to spot maker/taker fees.
/// @note Values based on Binance spot fee schedule (without BNB fee-discount modeling).
const std::map<int, FeeRate> spot_vip_fee_rates = {
    {0, {0.00100, 0.00100}},
    {1, {0.00090, 0.00100}},
    {2, {0.00080, 0.00100}},
    {3, {0.00040, 0.00060}},
    {4, {0.00040, 0.00052}},
    {5, {0.00025, 0.00031}},
    {6, {0.00020, 0.00029}},
    {7, {0.00019, 0.00028}},
    {8, {0.00016, 0.00025}},
    {9, {0.00011, 0.00023}}
};


/// @brief Defines a maintenance margin tier based on notional size.
struct MarginTier {
    double notional_upper;          ///< Upper bound of notional for this tier
    double maintenance_margin_rate; ///< Maintenance margin % of notional
    double max_leverage;            ///< Max leverage allowed
};


/// @brief Tiered margin configuration for position sizing and maintenance.
const std::vector<MarginTier> margin_tiers = {
    {   50000,   0.0040, 125},
    {  600000,   0.0050, 100},
    { 3000000,   0.0065,  75},
    {12000000,   0.0100,  50},
    {70000000,   0.0200,  25},
    {100000000,  0.0250,  20},
    {230000000,  0.0500,  10},
    {480000000,  0.1000,   5},
    {600000000,  0.1250,   4},
    {800000000,  0.1500,   3},
    {1200000000, 0.2500,   2},
    {std::numeric_limits<double>::max(), 0.5000, 1}
};
